# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0

import asyncio
import inspect
import logging
import struct
from collections.abc import Callable
from dataclasses import dataclass

import bt_sim_hci as sim_hci
import bt_sim_ll as sim_ll
from bumble import hci, lmp
from bumble.controller import Connection, ScoLink
from bumble.controller import Controller as controller
from bumble.core import PhysicalTransport

logger = logging.getLogger(__name__)


class bt_classic_discovery:
    def __init__(
        self,
        iac_lap,
        count,
        interval=1.25,
        on_inquiry: Callable | None = None,
        on_complete: Callable | None = None,
    ):
        self.flag = False
        self.count = count
        self.interval = interval
        self._task = None
        self.on_inquiry = on_inquiry
        self.on_complete = on_complete
        self._is_running = False
        self.iac_lap = iac_lap

    def start(
        self,
        count,
        interval=1.25,
        on_inquiry: Callable | None = None,
        on_complete: Callable | None = None,
    ):
        """Start periodic timeout callback"""
        self.flag = True
        self._is_running = True
        self.count = count
        self.interval = interval
        if on_inquiry:
            self.on_inquiry = on_inquiry
        if on_complete:
            self.on_complete = on_complete
        if self._task:
            self._task.cancel()
        self._task = asyncio.create_task(self._periodic_callback(interval))

    async def _periodic_callback(self, interval):
        """Execute callback periodically"""
        try:
            while self._is_running:
                await asyncio.sleep(interval)
                if self._is_running and self.on_inquiry:
                    if inspect.iscoroutinefunction(self.on_inquiry):
                        await self.on_inquiry()
                    else:
                        self.on_inquiry()
                if self.count > 0:
                    self.count = self.count - 1
                if self.count == 0 and self.on_complete:
                    if inspect.iscoroutinefunction(self.on_complete):
                        await self.on_complete()
                    else:
                        self.on_complete()
                    self.stop()
        except asyncio.CancelledError:
            pass

    def stop(self):
        """Stop periodic execution"""
        self.flag = False
        self._is_running = False
        if self._task:
            self._task.cancel()
            self._task = None


@dataclass(init=False)
class BtSimConnection(Connection):
    """Simulated BR/EDR connection state.

    This extends Bumble's `bumble.controller.Connection` with additional state used by the
    Bluetooth Classic simulation tests.
    Note:
        We set `init=False` so the dataclass does not generate its own `__init__`.
        That keeps the constructor signature identical to Bumble's `Connection`, which
        avoids static-analysis false positives like `unexpected-keyword-arg`.
    """

    authentication_request: bool = False
    numeric_comparison_done: bool = False
    numeric_comparison_failed: bool = False
    dhkey_check: bool = False
    authentication_done: bool = False
    encryption_enabled: int = 0
    link_key: int = 0
    link_key_type: int = 0
    key_size: int = 0


@dataclass
class BtSimScoLink(ScoLink):
    """Simulated SCO/eSCO link state.

    `established` is False while the link is being set up (handle not yet assigned).
    """

    air_mode: int = hci.HCI_Synchronous_Connection_Complete_Event.AirMode.CVSD
    initiator: bool = False
    established: bool = False


# Air Coding Format (Voice_Setting bits 0-1) to Air_Mode (HCI and LMP)
VOICE_SETTING_AIR_MODE = {
    0x00: hci.HCI_Synchronous_Connection_Complete_Event.AirMode.CVSD,
    0x01: hci.HCI_Synchronous_Connection_Complete_Event.AirMode.U_LAW_LOG,
    0x02: hci.HCI_Synchronous_Connection_Complete_Event.AirMode.A_LAW_LOG_AIR_MORE,
    0x03: hci.HCI_Synchronous_Connection_Complete_Event.AirMode.TRANSPARENT_DATA,
}

# EV3, EV4 and EV5 bits of the HCI Packet_Type parameter
ESCO_PACKET_TYPES = 0x0038


class bt_sim_controller(controller):
    def _find_classic_connection(self, key: int | hci.Address) -> BtSimConnection | None:
        """Lookup an ACL BR/EDR connection.

        Args:
            key: If `int`, treated as `connection_handle`. If `hci.Address`,
                treated as peer address.

        Returns:
            The `BtSimConnection` when found, otherwise `None`.
        """
        connect: Connection | None

        if isinstance(key, int):
            connect = None
            for _address, candidate in self.classic_connections.items():
                if candidate.handle == key:
                    connect = candidate
                    break
        elif isinstance(key, hci.Address):
            connect = self.classic_connections.get(key)
        else:
            logger.warning(
                'Unknown type %s',
                type(key),
            )
            return None

        if connect is None:
            logger.warning(
                'Connect[%s] not found',
                key,
            )
            return None

        if isinstance(connect, BtSimConnection):
            return connect

        logger.warning(
            '[%s] classic_connections entry is %s, expected BtSimConnection',
            self.name,
            type(connect),
        )
        return None

    # Enable BR/EDR feature
    # Byte 1: SCO link, HV2, HV3; byte 2: CVSD, transparent synchronous data;
    # byte 3: extended SCO link (EV3); byte 4: LE Supported (Controller)
    lmp_features: bytes = bytes.fromhex('0038098040000000')

    inquiry_mode: int = 0
    page_timeout: int = 0x2000
    default_link_policy_settings: int = 0
    discovery: bt_classic_discovery = None
    # Mutable defaults must not be shared across instances.
    # Keep as None here, initialize per-instance in __init__.
    iac_lap: list | None = None
    class_of_device: int = 0

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if self.iac_lap is None:
            self.iac_lap = [hci.HCI_GENERAL_INQUIRY_LAP]

    def start_inquiry(self, lap) -> None:
        logger.debug("[%s] >>> Inquiry LAP %s", self.name, lap)
        if self.link:
            self.link.start_inquiry(self, lap)

    def on_inquiry_complete(self) -> None:
        logger.debug("Inquiry complete")
        self.send_hci_packet(hci.HCI_Inquiry_Complete_Event(status=hci.HCI_SUCCESS))

    def on_inquiry_timeout(self) -> None:
        logger.debug("Inquiry timeout")
        self.start_inquiry(self.discovery.iac_lap)

    def on_hci_write_page_timeout_command(
        self, command: hci.HCI_Write_Page_Timeout_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.3.18 Write Page Timeout Command
        '''
        self.page_timeout = command.page_timeout
        return bytes([hci.HCI_SUCCESS])

    def on_hci_write_inquiry_mode_command(
        self, command: hci.HCI_Write_Inquiry_Mode_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.3.18 Write Inquiry Mode Command
        '''
        self.inquiry_mode = command.inquiry_mode
        return bytes([hci.HCI_SUCCESS])

    def on_hci_read_default_link_policy_settings_command(
        self, _command: sim_hci.HCI_Read_Default_Link_Policy_Settings_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec 7.2.11 Read Default Link Policy Settings command
        '''
        return struct.pack(
            '<BH',
            hci.HCI_SUCCESS,
            self.default_link_policy_settings,
        )

    def on_inquiry_result_ind(self, packet: sim_ll.InquiryInd) -> None:
        self.send_hci_packet(
            hci.HCI_Inquiry_Result_Event(
                bd_addr=packet.bd_address,
                page_scan_repetition_mode=packet.Page_Scan_Repetition_Mode,
                reserved_0=packet.Reserved,
                reserved_1=packet.Reserved,
                class_of_device=packet.Class_Of_Device,
                clock_offset=packet.Clock_Offset,
            )
        )

    def on_inquiry_result_with_rssi_ind(self, packet: sim_ll.InquiryRssiInd) -> None:
        self.send_hci_packet(
            hci.HCI_Inquiry_Result_With_RSSI_Event(
                bd_addr=packet.bd_address,
                page_scan_repetition_mode=packet.Page_Scan_Repetition_Mode,
                reserved=packet.Reserved,
                class_of_device=packet.Class_Of_Device,
                clock_offset=packet.Clock_Offset,
                rssi=packet.RSSI,
            )
        )

    def on_extended_inquiry_result_ind(self, packet: sim_ll.ExtendedInquiryInd) -> None:
        self.send_hci_packet(
            hci.HCI_Extended_Inquiry_Result_Event(
                num_responses=packet.num_responses,
                bd_addr=packet.bd_address,
                page_scan_repetition_mode=packet.Page_Scan_Repetition_Mode,
                reserved=packet.Reserved,
                class_of_device=packet.Class_Of_Device,
                clock_offset=packet.Clock_Offset,
                rssi=packet.RSSI,
                extended_inquiry_response=packet.Extended_Inquiry_Response,
            )
        )

    def on_inquiry_result_pdu(self, packet: sim_ll.InquiryPdu) -> None:
        logger.debug("[%s] <<< Inquiry PDU: %s", self.name, packet)
        if isinstance(packet, sim_ll.InquiryInd):
            self.on_inquiry_result_ind(packet)
        elif isinstance(packet, sim_ll.InquiryRssiInd):
            self.on_inquiry_result_with_rssi_ind(packet)
        elif isinstance(packet, sim_ll.ExtendedInquiryInd):
            self.on_extended_inquiry_result_ind(packet)
        else:
            logger.warning("[%s] Unknown inquiry PDU type: %s", self.name, type(packet))

    def on_hci_inquiry_command(self, command: hci.HCI_Inquiry_Command) -> None:
        '''
        See Bluetooth spec 7.1.1 Inquiry command
        '''
        logger.debug(
            f'Inquiry LAP {command.lap} length {command.inquiry_length} '
            f'num_responses {command.num_responses}'
        )

        self.discovery = bt_classic_discovery(
            command.lap,
            command.inquiry_length,
            on_inquiry=self.on_inquiry_timeout,
            on_complete=self.on_inquiry_complete,
        )
        self.discovery.start(command.inquiry_length)

        self.send_hci_packet(
            hci.HCI_Command_Status_Event(
                status=hci.HCI_SUCCESS,
                num_hci_command_packets=1,
                command_opcode=command.op_code,
            )
        )

        self.start_inquiry(command.lap)

    def on_hci_inquiry_cancel_command(
        self, _command: hci.HCI_Inquiry_Cancel_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec 7.1.2 Inquiry Cancel command
        '''
        if self.discovery is not None:
            self.discovery.stop()

        return bytes([hci.HCI_SUCCESS])

    def on_hci_write_current_iac_lap_command(
        self, command: sim_hci.HCI_Write_Current_Iac_Lap_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec 7.3.45 Write Current IAC LAP command
        '''
        self.iac_lap = []

        for iac in command.iac_lap:
            self.iac_lap.append(iac)
        return bytes([hci.HCI_SUCCESS])

    def on_hci_read_class_of_device_command(
        self, _command: hci.HCI_Read_Class_Of_Device_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.3.25 Read Class of Device Command
        '''
        return struct.pack('<BI', hci.HCI_SUCCESS, self.class_of_device)[:4]

    def on_hci_write_class_of_device_command(
        self, command: hci.HCI_Write_Class_Of_Device_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.3.26 Write Class of Device Command
        '''
        self.class_of_device = command.class_of_device
        return bytes([hci.HCI_SUCCESS])

    def on_hci_create_connection_command(
        self, command: hci.HCI_Create_Connection_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.1.5 Create Connection command
        '''

        if self.link is None:
            return None
        logger.debug(f'Connection request to {command.bd_addr}')

        # Check that we don't already have a pending connection
        if self.link.get_pending_connection():
            self.send_hci_packet(
                hci.HCI_Command_Status_Event(
                    status=hci.HCI_CONTROLLER_BUSY_ERROR,
                    num_hci_command_packets=1,
                    command_opcode=command.op_code,
                )
            )
            return None

        self.classic_connections[command.bd_addr] = BtSimConnection(
            controller=self,
            handle=0,
            role=hci.Role.CENTRAL,
            peer_address=command.bd_addr,
            link=self.link,
            transport=PhysicalTransport.BR_EDR,
            link_type=hci.HCI_Connection_Complete_Event.LinkType.ACL,
            classic_allow_role_switch=bool(command.allow_role_switch),
        )

        # Say that the connection is pending
        self.send_hci_packet(
            hci.HCI_Command_Status_Event(
                status=hci.HCI_COMMAND_STATUS_PENDING,
                num_hci_command_packets=1,
                command_opcode=command.op_code,
            )
        )
        future = self.send_lmp_packet(command.bd_addr, lmp.LmpHostConnectionReq())

        def on_response(future: asyncio.Future[int]):
            self.on_classic_connection_complete(command.bd_addr, future.result())

        future.add_done_callback(on_response)
        return None

    def on_classic_connection_request(self, peer_address: hci.Address, link_type: int) -> None:
        if link_type == hci.HCI_Connection_Complete_Event.LinkType.ACL:
            self.classic_connections[peer_address] = BtSimConnection(
                controller=self,
                handle=0,
                role=hci.Role.PERIPHERAL,
                peer_address=peer_address,
                link=self.link,
                transport=PhysicalTransport.BR_EDR,
                link_type=link_type,
                classic_allow_role_switch=self.classic_allow_role_switch,
            )
        else:
            self.sco_links[peer_address] = ScoLink(
                handle=0,
                link_type=link_type,
                peer_address=peer_address,
            )
        self.send_hci_packet(
            hci.HCI_Connection_Request_Event(
                bd_addr=peer_address,
                class_of_device=self.class_of_device,
                link_type=link_type,
            )
        )

    def on_classic_connection_complete(self, peer_address: hci.Address, status: int) -> None:
        super().on_classic_connection_complete(peer_address, status)
        if status != hci.HCI_SUCCESS:
            # Drop the entry created by Create Connection or the connection request
            self.classic_connections.pop(peer_address, None)

    def on_classic_disconnected(self, peer_address: hci.Address, reason: int) -> None:
        # A synchronous link cannot outlive its ACL link, release it first.
        self._release_sco_link(peer_address, reason)
        super().on_classic_disconnected(peer_address, reason)

    def on_lmp_packet(self, sender_address: hci.Address, packet: lmp.Packet):
        if isinstance(packet, lmp.LmpEscoLinkReq):
            self._on_sco_link_req(
                sender_address, hci.HCI_Connection_Complete_Event.LinkType.ESCO, packet.air_mode
            )
        elif isinstance(packet, lmp.LmpScoLinkReq):
            self._on_sco_link_req(
                sender_address, hci.HCI_Connection_Complete_Event.LinkType.SCO, packet.air_mode
            )
        else:
            super().on_lmp_packet(sender_address, packet)

    def _send_sco_link_rsp(
        self, peer_address: hci.Address, link_type: int, status: int = hci.HCI_SUCCESS
    ) -> None:
        if link_type == hci.HCI_Connection_Complete_Event.LinkType.ESCO:
            if status == hci.HCI_SUCCESS:
                packet = lmp.LmpAcceptedExt(lmp.Opcode.LMP_ESCO_LINK_REQ)
            else:
                packet = lmp.LmpNotAcceptedExt(lmp.Opcode.LMP_ESCO_LINK_REQ, status)
        else:
            if status == hci.HCI_SUCCESS:
                packet = lmp.LmpAccepted(lmp.Opcode.LMP_SCO_LINK_REQ)
            else:
                packet = lmp.LmpNotAccepted(lmp.Opcode.LMP_SCO_LINK_REQ, status)

        self.link.send_lmp_packet(self, peer_address, packet)

    def _on_sco_link_req(self, peer_address: hci.Address, link_type: int, air_mode: int) -> None:
        if self._find_classic_connection(peer_address) is None or peer_address in self.sco_links:
            self._send_sco_link_rsp(peer_address, link_type, hci.HCI_COMMAND_DISALLOWED_ERROR)
            return

        self.sco_links[peer_address] = BtSimScoLink(
            handle=0,
            link_type=link_type,
            peer_address=peer_address,
            air_mode=air_mode,
            initiator=False,
        )

        peer = self.link.find_classic_controller(peer_address)
        self.send_hci_packet(
            hci.HCI_Connection_Request_Event(
                bd_addr=peer_address,
                class_of_device=peer.class_of_device if peer is not None else 0,
                link_type=link_type,
            )
        )

    def _complete_sco_link(self, peer_address: hci.Address, status: int) -> None:
        sco_link = self.sco_links.get(peer_address)
        if not isinstance(sco_link, BtSimScoLink) or sco_link.established:
            return

        esco = sco_link.link_type == hci.HCI_Connection_Complete_Event.LinkType.ESCO
        if status == hci.HCI_SUCCESS:
            sco_link.handle = self.allocate_connection_handle()
            sco_link.established = True
            handle = sco_link.handle
            logger.debug("[%s] SCO link 0x%04X to %s", self.name, handle, peer_address)
        else:
            self.sco_links.pop(peer_address, None)
            handle = 0

        self.send_hci_packet(
            hci.HCI_Synchronous_Connection_Complete_Event(
                status=status,
                connection_handle=handle,
                bd_addr=peer_address,
                link_type=sco_link.link_type,
                transmission_interval=0x06 if status == hci.HCI_SUCCESS else 0,
                retransmission_window=0x02 if esco and status == hci.HCI_SUCCESS else 0,
                rx_packet_length=60 if status == hci.HCI_SUCCESS else 0,
                tx_packet_length=60 if status == hci.HCI_SUCCESS else 0,
                air_mode=sco_link.air_mode,
            )
        )

    def _release_sco_link(self, peer_address: hci.Address, reason: int) -> None:
        sco_link = self.sco_links.get(peer_address)
        if not isinstance(sco_link, BtSimScoLink):
            return

        if sco_link.established:
            self.on_classic_sco_disconnected(peer_address, reason)
            return

        # Setup is still in progress: abort the LMP transaction and fail the setup
        pending = self.classic_pending_commands.get(peer_address, {})
        for opcode in (lmp.Opcode.LMP_ESCO_LINK_REQ, lmp.Opcode.LMP_SCO_LINK_REQ):
            future = pending.pop(opcode, None)
            if future is not None and not future.done():
                future.cancel()
        self._complete_sco_link(peer_address, reason)

    def on_hci_setup_synchronous_connection_command(
        self, command: sim_hci.HCI_Setup_Synchronous_Connection_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.1.26 Setup Synchronous Connection command
        '''
        if self.link is None:
            return None

        connect = self._find_classic_connection(command.connection_handle)

        if connect is None:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        elif connect.peer_address in self.sco_links:
            status = hci.HCI_COMMAND_DISALLOWED_ERROR
        else:
            status = hci.HCI_SUCCESS

        self.send_hci_packet(
            hci.HCI_Command_Status_Event(
                status=status,
                num_hci_command_packets=1,
                command_opcode=command.op_code,
            )
        )

        if status != hci.HCI_SUCCESS:
            return None

        peer_address = connect.peer_address
        air_mode = VOICE_SETTING_AIR_MODE[command.voice_setting & 0x03]
        if command.packet_type & ESCO_PACKET_TYPES:
            link_type = hci.HCI_Connection_Complete_Event.LinkType.ESCO
            packet = lmp.LmpEscoLinkReq(
                esco_handle=0,
                esco_lt_addr=0,
                timing_control_flags=0,
                d_esco=0,
                t_esco=0x06,
                w_esco=0x02,
                esco_packet_type_c_to_p=0,
                esco_packet_type_p_to_c=0,
                packet_length_c_to_p=60,
                packet_length_p_to_c=60,
                air_mode=air_mode,
                negotiation_state=0,
            )
        else:
            link_type = hci.HCI_Connection_Complete_Event.LinkType.SCO
            packet = lmp.LmpScoLinkReq(
                sco_handle=0,
                timing_control_flags=0,
                d_sco=0,
                t_sco=0x06,
                sco_packet=0,
                air_mode=air_mode,
            )

        self.sco_links[peer_address] = BtSimScoLink(
            handle=0,
            link_type=link_type,
            peer_address=peer_address,
            air_mode=air_mode,
            initiator=True,
        )

        future = self.send_lmp_packet(peer_address, packet)

        def on_response(future: asyncio.Future[int]):
            if future.cancelled():
                return
            self._complete_sco_link(peer_address, future.result())

        future.add_done_callback(on_response)
        return None

    def on_hci_accept_synchronous_connection_request_command(
        self, command: sim_hci.HCI_Accept_Synchronous_Connection_Request_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.1.27 Accept Synchronous Connection Request command
        '''
        if self.link is None:
            return None

        sco_link = self.sco_links.get(command.bd_addr)

        if not isinstance(sco_link, BtSimScoLink) or sco_link.initiator or sco_link.established:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        else:
            status = hci.HCI_SUCCESS

        self.send_hci_packet(
            hci.HCI_Command_Status_Event(
                status=status,
                num_hci_command_packets=1,
                command_opcode=command.op_code,
            )
        )

        if status != hci.HCI_SUCCESS:
            return None

        air_mode = VOICE_SETTING_AIR_MODE[command.voice_setting & 0x03]
        if air_mode != sco_link.air_mode:
            # Both sides report the air mode requested by the initiator
            logger.warning(
                "[%s] SCO air mode mismatch with %s: initiator %d, acceptor %d",
                self.name,
                command.bd_addr,
                sco_link.air_mode,
                air_mode,
            )

        self._send_sco_link_rsp(command.bd_addr, sco_link.link_type)
        self._complete_sco_link(command.bd_addr, hci.HCI_SUCCESS)
        return None

    def on_hci_reject_connection_request_command(
        self, command: hci.HCI_Reject_Connection_Request_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.1.9 Reject Connection Request command
        '''
        if self.link is None:
            return None

        sco_link = self.sco_links.get(command.bd_addr)
        connect = self.classic_connections.get(command.bd_addr)

        if (
            isinstance(sco_link, BtSimScoLink)
            and not sco_link.initiator
            and not sco_link.established
        ):
            status = hci.HCI_SUCCESS
        elif connect is not None and connect.handle == 0 and connect.role == hci.Role.PERIPHERAL:
            sco_link = None
            status = hci.HCI_SUCCESS
        else:
            sco_link = None
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR

        self.send_hci_packet(
            hci.HCI_Command_Status_Event(
                status=status,
                num_hci_command_packets=1,
                command_opcode=command.op_code,
            )
        )

        if status != hci.HCI_SUCCESS:
            return None

        if sco_link is not None:
            self._send_sco_link_rsp(command.bd_addr, sco_link.link_type, command.reason)
            self._complete_sco_link(command.bd_addr, command.reason)
        else:
            self.link.send_lmp_packet(
                self,
                command.bd_addr,
                lmp.LmpNotAccepted(lmp.Opcode.LMP_HOST_CONNECTION_REQ, command.reason),
            )
            self.on_classic_connection_complete(command.bd_addr, command.reason)
        return None

    def on_read_remote_supported_features_pdu(
        self, lmp_features: bytes, connection_handle: int
    ) -> None:
        logger.debug("[%s] <<< Read Remote Supported Features PDU %s", self.name, lmp_features)
        self.send_hci_packet(
            hci.HCI_Read_Remote_Supported_Features_Complete_Event(
                status=hci.HCI_SUCCESS,
                connection_handle=connection_handle,
                lmp_features=lmp_features,
            )
        )

    def read_remote_supported_features(
        self, peer_address: hci.Address, connection_handle: int
    ) -> None:
        logger.debug("[%s] >>> Peer Address %s", self.name, peer_address)
        if self.link:
            self.link.read_remote_supported_features(self, peer_address, connection_handle)
        else:
            logger.warning("[%s] No link available", self.name)

    def on_hci_read_remote_supported_features_command(
        self, command: hci.HCI_Read_Remote_Supported_Features_Command
    ) -> None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.1.21 Read Remote Supported Features Command
        '''
        logger.debug(f'Connect handle {command.connection_handle}')

        connect = self._find_classic_connection(command.connection_handle)

        if connect is None:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        else:
            status = hci.HCI_SUCCESS

        self.send_hci_packet(
            hci.HCI_Command_Status_Event(
                status=status,
                num_hci_command_packets=1,
                command_opcode=command.op_code,
            )
        )

        if connect is not None:
            self.read_remote_supported_features(connect.peer_address, command.connection_handle)
        else:
            logger.warning("[%s] Unknown connection handle", self.name)

    def on_hci_host_number_of_completed_packets_command(
        self, command: sim_hci.HCI_Host_Number_Of_Completed_Packets_Command
    ) -> None:
        '''
        See Bluetooth spec Vol 4, Part E - 6.27 Host Number of Completed Packets Command
        '''
        logger.debug(
            f'Connect handle {command.connection_handle}'
            f'num_of_completed_packets {command.num_of_completed_packets}'
        )

    def on_hci_authentication_requested_command(
        self, command: hci.HCI_Authentication_Requested_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.2.7 Authentication Requested Command
        '''
        logger.debug(f'Connect handle {command.connection_handle}')

        connect = self._find_classic_connection(command.connection_handle)

        if connect is None:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        else:
            status = hci.HCI_SUCCESS
            if connect.authentication_request:
                status = hci.HCI_HOST_BUSY_PAIRING_ERROR
            else:
                connect.authentication_request = True

        self.send_hci_packet(
            hci.HCI_Command_Status_Event(
                status=status,
                num_hci_command_packets=1,
                command_opcode=command.op_code,
            )
        )

        if connect is None:
            return None

        self.send_hci_packet(
            hci.HCI_Link_Key_Request_Event(
                bd_addr=connect.peer_address,
            )
        )
        return None

    def on_hci_link_key_request_negative_reply_command(
        self, command: hci.HCI_Link_Key_Request_Negative_Reply_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.2.8 Link Key Request Negative Reply Command
        '''
        logger.debug(f'BD Address {command.bd_addr}')

        connect = self._find_classic_connection(command.bd_addr)

        if connect is None:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        else:
            status = hci.HCI_SUCCESS

        return_parameters = (
            hci.HCI_Link_Key_Request_Negative_Reply_Command.create_return_parameters(
                status=status,
                bd_addr=command.bd_addr,
            )
        )

        self.send_hci_packet(
            hci.HCI_Command_Complete_Event(
                num_hci_command_packets=1,
                command_opcode=command.op_code,
                return_parameters=return_parameters,
            )
        )

        if status != hci.HCI_SUCCESS:
            return None

        self.send_hci_packet(
            hci.HCI_IO_Capability_Request_Event(
                bd_addr=command.bd_addr,
            )
        )
        return None

    def on_hci_io_capability_request_reply_command(
        self, command: hci.HCI_IO_Capability_Request_Reply_Command
    ) -> bytes | None:
        '''
        See Bluetooth spec Vol 4, Part E - 7.2.11 IO Capability Request Reply Command
        '''
        logger.debug(f'BD Address {command.bd_addr} IO CAP {command.io_capability}')

        connect = self._find_classic_connection(command.bd_addr)

        if connect is None:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        else:
            status = hci.HCI_SUCCESS

        return_parameters = hci.HCI_IO_Capability_Request_Reply_Command.create_return_parameters(
            status=status,
            bd_addr=command.bd_addr,
        )

        self.send_hci_packet(
            hci.HCI_Command_Complete_Event(
                num_hci_command_packets=1,
                command_opcode=command.op_code,
                return_parameters=return_parameters,
            )
        )

        if connect is None:
            return None

        if self.link:
            if connect.authentication_request:
                self.link.io_capability_request(
                    self,
                    command.bd_addr,
                    command.io_capability,
                    command.oob_data_present,
                    command.authentication_requirements,
                )
            else:
                self.link.io_capability_response(
                    self,
                    command.bd_addr,
                    command.io_capability,
                    command.oob_data_present,
                    command.authentication_requirements,
                )
        return None

    def on_hci_io_capability_response_ind(
        self,
        bd_address,
        io_capability,
        oob_data_present,
        authentication_requirements,
    ) -> None:
        self.send_hci_packet(
            hci.HCI_IO_Capability_Response_Event(
                bd_addr=bd_address,
                io_capability=io_capability,
                oob_data_present=oob_data_present,
                authentication_requirements=authentication_requirements,
            )
        )

    def on_hci_io_capability_request_ind(
        self,
        bd_address,
    ) -> None:
        connect = self._find_classic_connection(bd_address)
        if connect is None:
            return

        connect.numeric_comparison_done = False
        connect.numeric_comparison_failed = False
        connect.dhkey_check = False
        connect.authentication_done = False

        self.send_hci_packet(
            hci.HCI_IO_Capability_Request_Event(
                bd_addr=bd_address,
            )
        )

    def on_hci_user_confirmation_request_ind(
        self,
        bd_address,
        numeric_value,
    ) -> None:
        self.send_hci_packet(
            hci.HCI_User_Confirmation_Request_Event(
                bd_addr=bd_address,
                numeric_value=numeric_value,
            )
        )

    def on_hci_user_confirmation_request_reply_command(
        self, command: hci.HCI_User_Confirmation_Request_Reply_Command
    ) -> bytes | None:
        logger.debug(f'BD Address {command.bd_addr}')

        connect = self._find_classic_connection(command.bd_addr)

        if connect is None:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        else:
            status = hci.HCI_SUCCESS

        if connect is not None:
            connect.numeric_comparison_done = True
            connect.numeric_comparison_failed = False

        return_parameters = (
            hci.HCI_User_Confirmation_Request_Reply_Command.create_return_parameters(
                status=status,
                bd_addr=command.bd_addr,
            )
        )

        self.send_hci_packet(
            hci.HCI_Command_Complete_Event(
                num_hci_command_packets=1,
                command_opcode=command.op_code,
                return_parameters=return_parameters,
            )
        )

        if connect is None:
            return None

        if connect.authentication_request:
            self.link.dhkey_check(
                self,
                command.bd_addr,
            )
        elif connect.dhkey_check:
            self.link.dhkey_check_accepted(
                self,
                command.bd_addr,
            )
        return None

    def on_hci_user_confirmation_request_negative_reply_command(
        self, command: hci.HCI_User_Confirmation_Request_Negative_Reply_Command
    ) -> bytes | None:
        logger.debug(f'BD Address {command.bd_addr}')

        connect = self._find_classic_connection(command.bd_addr)

        if connect is None:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        else:
            status = hci.HCI_SUCCESS
            connect.numeric_comparison_done = True
            connect.numeric_comparison_failed = True

        return_parameters = (
            hci.HCI_User_Confirmation_Request_Negative_Reply_Command.create_return_parameters(
                status=status,
                bd_addr=command.bd_addr,
            )
        )

        self.send_hci_packet(
            hci.HCI_Command_Complete_Event(
                num_hci_command_packets=1,
                command_opcode=command.op_code,
                return_parameters=return_parameters,
            )
        )

        if connect is None:
            return None

        if connect.authentication_request:
            self.link.numeric_comparison_failed(
                self,
                command.bd_addr,
            )
        elif connect.dhkey_check:
            self.link.dhkey_check_not_accepted(
                self,
                command.bd_addr,
            )
        return None

    def on_numeric_comparison_failed_ind(
        self,
        bd_address,
    ) -> None:
        connect = self._find_classic_connection(bd_address)
        if connect is None:
            return

        connect.authentication_request = False

        if connect.authentication_done:
            return

        connect.authentication_done = True

        self.send_hci_packet(
            hci.HCI_Simple_Pairing_Complete_Event(
                status=hci.HCI_AUTHENTICATION_FAILURE_ERROR,
                bd_addr=bd_address,
            )
        )

    def on_dhkey_check_ind(
        self,
        bd_address,
    ) -> None:
        connect = self._find_classic_connection(bd_address)
        if connect is None:
            return

        if connect.authentication_request:
            return

        if connect.authentication_done:
            return

        if connect.numeric_comparison_done:
            if connect.numeric_comparison_failed:
                self.link.dhkey_check_not_accepted(
                    self,
                    bd_address,
                )
            else:
                self.link.dhkey_check_accepted(
                    self,
                    bd_address,
                )
        else:
            connect.dhkey_check = True

    def on_dhkey_check_not_accepted_ind(
        self,
        bd_address,
    ) -> None:
        connect = self._find_classic_connection(bd_address)
        if connect is None:
            return

        connect.authentication_request = False

        if connect.authentication_done:
            return

        connect.authentication_done = True

        self.send_hci_packet(
            hci.HCI_Simple_Pairing_Complete_Event(
                status=hci.HCI_AUTHENTICATION_FAILURE_ERROR,
                bd_addr=bd_address,
            )
        )

    def on_dhkey_check_accepted_ind(
        self,
        bd_address,
    ) -> None:
        connect = self._find_classic_connection(bd_address)
        if connect is None:
            return

        if connect.authentication_done:
            return

        connect.authentication_done = True

        self.send_hci_packet(
            hci.HCI_Simple_Pairing_Complete_Event(
                status=hci.HCI_SUCCESS,
                bd_addr=bd_address,
            )
        )

    def on_link_key_notification_ind(
        self,
        bd_address,
        link_key,
        link_key_type,
        key_size,
    ) -> None:
        connect = self._find_classic_connection(bd_address)
        if connect is None:
            return

        self.send_hci_packet(
            hci.HCI_Link_Key_Notification_Event(
                bd_addr=bd_address,
                link_key=link_key,
                key_type=link_key_type,
            )
        )
        connect.link_key = link_key
        connect.link_key_type = link_key_type
        connect.key_size = key_size

        if connect.authentication_request:
            status = hci.HCI_SUCCESS
            connect.authentication_request = False
            self.send_hci_packet(
                hci.HCI_Authentication_Complete_Event(
                    status=status,
                    connection_handle=connect.handle,
                )
            )

    def on_hci_set_connection_encryption_command(
        self, command: hci.HCI_Set_Connection_Encryption_Command
    ) -> bytes | None:
        logger.debug(f'Connection handle {command.connection_handle}')

        connect = self._find_classic_connection(command.connection_handle)

        if connect is None:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        else:
            status = hci.HCI_SUCCESS

        self.send_hci_packet(
            hci.HCI_Command_Status_Event(
                status=status,
                num_hci_command_packets=1,
                command_opcode=command.op_code,
            )
        )

        if status != hci.HCI_SUCCESS:
            return None

        if command.encryption_enable != 0:
            self.link.start_encryption_req(
                self,
                connect.peer_address,
            )

        return None

    def on_start_encryption_req_accepted_ind(
        self,
        bd_address,
        encryption_enabled,
    ) -> None:
        connect = self._find_classic_connection(bd_address)
        if connect is None:
            return

        connect.encryption_enabled = encryption_enabled

        self.send_hci_packet(
            hci.HCI_Encryption_Change_Event(
                status=hci.HCI_SUCCESS,
                connection_handle=connect.handle,
                encryption_enabled=encryption_enabled,
            )
        )

    def on_hci_read_encryption_key_size_command(
        self, command: hci.HCI_Read_Encryption_Key_Size_Command
    ) -> None:
        logger.debug(f'Connection handle {command.connection_handle}')

        connect = self._find_classic_connection(command.connection_handle)
        key_size = 0

        if connect is None:
            status = hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        else:
            status = hci.HCI_SUCCESS
            key_size = connect.key_size

        return_parameters = hci.HCI_Read_Encryption_Key_Size_Command.create_return_parameters(
            status=status,
            connection_handle=command.connection_handle,
            key_size=key_size,
        )

        self.send_hci_packet(
            hci.HCI_Command_Complete_Event(
                num_hci_command_packets=1,
                command_opcode=command.op_code,
                return_parameters=return_parameters,
            )
        )
