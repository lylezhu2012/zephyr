# Copyright 2025 NXP
#
# SPDX-License-Identifier: Apache-2.0

import dataclasses
from collections.abc import Sequence
from dataclasses import field

from bumble.hci import STATUS_SPEC, Address, HCI_Command, metadata


@HCI_Command.command
@dataclasses.dataclass
class HCI_Read_Default_Link_Policy_Settings_Command(HCI_Command):
    '''
    See Bluetooth spec @ 7.2.11 Read Default Link Policy Settings command
    '''

    return_parameters_fields = [
        ('status', STATUS_SPEC),
        ('Default_Link_Policy_Settings', 2),
    ]


@HCI_Command.command
@dataclasses.dataclass
class HCI_Write_Current_Iac_Lap_Command(HCI_Command):
    '''
    See Bluetooth spec @ 7.3.45 Write Current IAC LAP command
    '''

    iac_lap: Sequence[int] = field(metadata=metadata(3, list_begin=True, list_end=True))


@HCI_Command.command
@dataclasses.dataclass
class HCI_Host_Number_Of_Completed_Packets_Command(HCI_Command):
    '''
    See Bluetooth spec @ 7.3.40 Host Number Of Completed Packets command
    '''

    connection_handle: Sequence[int] = field(metadata=metadata(2, list_begin=True, list_end=True))
    num_of_completed_packets: int = field(metadata=metadata(2))


@HCI_Command.command
@dataclasses.dataclass
class HCI_Setup_Synchronous_Connection_Command(HCI_Command):
    '''
    See Bluetooth spec @ 7.1.26 Setup Synchronous Connection command
    '''

    connection_handle: int = field(metadata=metadata(2))
    transmit_bandwidth: int = field(metadata=metadata(4))
    receive_bandwidth: int = field(metadata=metadata(4))
    max_latency: int = field(metadata=metadata(2))
    voice_setting: int = field(metadata=metadata(2))
    retransmission_effort: int = field(metadata=metadata(1))
    packet_type: int = field(metadata=metadata(2))


@HCI_Command.command
@dataclasses.dataclass
class HCI_Accept_Synchronous_Connection_Request_Command(HCI_Command):
    '''
    See Bluetooth spec @ 7.1.27 Accept Synchronous Connection Request command
    '''

    bd_addr: Address = field(metadata=metadata(Address.parse_address))
    transmit_bandwidth: int = field(metadata=metadata(4))
    receive_bandwidth: int = field(metadata=metadata(4))
    max_latency: int = field(metadata=metadata(2))
    voice_setting: int = field(metadata=metadata(2))
    retransmission_effort: int = field(metadata=metadata(1))
    packet_type: int = field(metadata=metadata(2))
