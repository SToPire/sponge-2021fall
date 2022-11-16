#include "network_interface.hh"

#include "arp_message.hh"
#include "buffer.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "ipv4_datagram.hh"
#include "parser.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    auto it = _mapping.find(next_hop_ip);
    EthernetFrame frame;
    EthernetHeader hdr;
    ARPMessage arp_msg;

    /* We don't know the MAC addr of this IP, send a ARP request. */
    if (it == _mapping.end()) {
        _pending_list.push_back({dgram, next_hop});

        /* The same IP is being queryed. Do not flood the network with ARP requests. */
        if (_blocking_mapping.find(next_hop_ip) != _blocking_mapping.end())
            return;
        _blocking_mapping.insert({next_hop_ip, _current_time});

        arp_msg.opcode = ARPMessage::OPCODE_REQUEST;
        /* Tell the others who we are. */
        arp_msg.sender_ip_address = _ip_address.ipv4_numeric();
        arp_msg.sender_ethernet_address = _ethernet_address;
        /* We want to query next_hop_ip's corresponding MAC addr. */
        arp_msg.target_ip_address = next_hop_ip;
        /* We doesn't know target MAC addr yet. */
        arp_msg.target_ethernet_address = EthernetAddress();

        /* The ARP request is broadcasted. */
        hdr.dst = ETHERNET_BROADCAST;
        hdr.src = _ethernet_address;
        hdr.type = EthernetHeader::TYPE_ARP;

        frame.header() = hdr;
        frame.payload() = BufferList(arp_msg.serialize());
    } else {
        hdr.dst = it->second.first;
        hdr.src = _ethernet_address;
        hdr.type = EthernetHeader::TYPE_IPv4;

        frame.header() = hdr;
        frame.payload() = BufferList(dgram.serialize());
    }
    _frames_out.push(frame);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    /* The frame is not for me. */
    if (frame.header().dst != _ethernet_address and frame.header().dst != ETHERNET_BROADCAST)
        return {};

    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage msg;
        if (msg.parse(frame.payload()) != ParseResult::NoError)
            return {};

        /* No matter it's a ARP request or reply, we should record sender's IP=>MAC mapping. */
        _mapping.insert({msg.sender_ip_address, {msg.sender_ethernet_address, _current_time}});

        if (msg.opcode == ARPMessage::OPCODE_REPLY) {
            /* Although theoretically impossible,
             * we use a tmp list to avoid pending list being modified in `send_datagram()`.
             */
            std::list<std::pair<InternetDatagram, Address>> tmp_list;
            for (auto it = _pending_list.begin(); it != _pending_list.end();) {
                /* Now, we know the MAC addr of this pending datagram's target. */
                if (it->second.ipv4_numeric() == msg.sender_ip_address) {
                    tmp_list.push_back(*it);
                    it = _pending_list.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto &e : tmp_list)
                send_datagram(e.first, e.second);
        } else if (msg.opcode == ARPMessage::OPCODE_REQUEST) {
            /* I'm not who queryer cares about. */
            if (msg.target_ip_address != _ip_address.ipv4_numeric())
                return {};

            ARPMessage reply_msg;
            EthernetFrame reply_frame;
            EthernetHeader reply_hdr;

            reply_msg.opcode = ARPMessage::OPCODE_REPLY;
            /* Tell the queryer who we are. */
            reply_msg.sender_ethernet_address = _ethernet_address;
            reply_msg.sender_ip_address = _ip_address.ipv4_numeric();
            /* In ARP reply, target is the queryer itself. */
            reply_msg.target_ethernet_address = msg.sender_ethernet_address;
            reply_msg.target_ip_address = msg.sender_ip_address;

            reply_hdr.dst = msg.sender_ethernet_address;
            reply_hdr.src = _ethernet_address;
            reply_hdr.type = EthernetHeader::TYPE_ARP;

            reply_frame.header() = reply_hdr;
            reply_frame.payload() = BufferList(reply_msg.serialize());

            _frames_out.push(reply_frame);
        }
    } else if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram datagram;
        if (datagram.parse(frame.payload()) == ParseResult::NoError)
            return datagram;
    }

    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _current_time += ms_since_last_tick;

    /* delete outdated IP=>MAC mapping */
    for (auto it = _mapping.begin(); it != _mapping.end();) {
        if (it->second.second + ARP_VALID_TIME <= _current_time) {
            it = _mapping.erase(it);
        } else {
            ++it;
        }
    }

    /* To prevent a ARP request storm, we have blocked a same-destination IP datagram long enouth. */
    for (auto it = _blocking_mapping.begin(); it != _blocking_mapping.end();) {
        if (it->second + ARP_REQ_BLOCKING_TIME <= _current_time) {
            it = _blocking_mapping.erase(it);
        } else {
            ++it;
        }
    }
}
