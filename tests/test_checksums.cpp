#include <gtest/gtest.h>

#include <random>

#include "bm_sim/checksums.h"
#include "bm_sim/parser.h"

/* Frame (66 bytes) */
static const unsigned char raw_tcp_pkt[66] = {
  0x00, 0x18, 0x0a, 0x05, 0x5a, 0x10, 0xa0, 0x88, /* ....Z... */
  0x69, 0x0c, 0xc3, 0x03, 0x08, 0x00, 0x45, 0x00, /* i.....E. */
  0x00, 0x34, 0x70, 0x90, 0x40, 0x00, 0x40, 0x06, /* .4p.@.@. */
  0x35, 0x08, 0x0a, 0x36, 0xc1, 0x21, 0x4e, 0x28, /* 5..6.!N( */
  0x7b, 0xac, 0xa2, 0x97, 0x00, 0x50, 0x7f, 0xc2, /* {....P.. */
  0x4c, 0x80, 0x39, 0x77, 0xec, 0xd9, 0x80, 0x10, /* L.9w.... */
  0x00, 0x44, 0x13, 0xcd, 0x00, 0x00, 0x01, 0x01, /* .D...... */
  0x08, 0x0a, 0x00, 0xc3, 0x6d, 0x86, 0xa8, 0x20, /* ....m..  */
  0x21, 0x9b                                      /* !. */
};

/* Frame (82 bytes) */
static const unsigned char raw_udp_pkt[82] = {
  0x8c, 0x04, 0xff, 0xac, 0x28, 0xa0, 0xa0, 0x88, /* ....(... */
  0x69, 0x0c, 0xc3, 0x03, 0x08, 0x00, 0x45, 0x00, /* i.....E. */
  0x00, 0x44, 0x3a, 0xf5, 0x40, 0x00, 0x40, 0x11, /* .D:.@.@. */
  0x5f, 0x0f, 0x0a, 0x00, 0x00, 0x0f, 0x4b, 0x4b, /* _.....KK */
  0x4b, 0x4b, 0x1f, 0x5c, 0x00, 0x35, 0x00, 0x30, /* KK.\.5.0 */
  0xeb, 0x61, 0x85, 0xa6, 0x01, 0x00, 0x00, 0x01, /* .a...... */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x61, /* .......a */
  0x70, 0x69, 0x03, 0x6e, 0x65, 0x77, 0x0a, 0x6c, /* pi.new.l */
  0x69, 0x76, 0x65, 0x73, 0x74, 0x72, 0x65, 0x61, /* ivestrea */
  0x6d, 0x03, 0x63, 0x6f, 0x6d, 0x00, 0x00, 0x01, /* m.com... */
  0x00, 0x01                                      /* .. */
};

// Google Test fixture for checksums tests
class ChecksumTest : public ::testing::Test {
protected:

  PHV phv;
  HeaderType ethernetHeaderType, ipv4HeaderType, udpHeaderType, tcpHeaderType;
  ParseState ethernetParseState, ipv4ParseState, udpParseState, tcpParseState;
  header_id_t ethernetHeader{0}, ipv4Header{1}, udpHeader{2}, tcpHeader{3};

  Parser parser;

  std::uniform_int_distribution<char> rd;

  ChecksumTest()
    : phv(4),
      ethernetHeaderType("ethernet_t", 0), ipv4HeaderType("ipv4_t", 1),
      udpHeaderType("udp_t", 2), tcpHeaderType("tcp_t", 3),
      ethernetParseState("parse_ethernet"),
      ipv4ParseState("parse_ipv4"),
      udpParseState("parse_udp"),
      tcpParseState("parse_tcp"),
      parser("test_parser", 0) {
    ethernetHeaderType.push_back_field("dstAddr", 48);
    ethernetHeaderType.push_back_field("srcAddr", 48);
    ethernetHeaderType.push_back_field("ethertype", 16);

    ipv4HeaderType.push_back_field("version", 4);
    ipv4HeaderType.push_back_field("ihl", 4);
    ipv4HeaderType.push_back_field("diffserv", 8);
    ipv4HeaderType.push_back_field("len", 16);
    ipv4HeaderType.push_back_field("id", 16);
    ipv4HeaderType.push_back_field("flags", 3);
    ipv4HeaderType.push_back_field("flagOffset", 13);
    ipv4HeaderType.push_back_field("ttl", 8);
    ipv4HeaderType.push_back_field("protocol", 8);
    ipv4HeaderType.push_back_field("checksum", 16);
    ipv4HeaderType.push_back_field("srcAddr", 32);
    ipv4HeaderType.push_back_field("dstAddr", 32);

    udpHeaderType.push_back_field("srcPort", 16);
    udpHeaderType.push_back_field("dstPort", 16);
    udpHeaderType.push_back_field("length", 16);
    udpHeaderType.push_back_field("checksum", 16);

    tcpHeaderType.push_back_field("srcPort", 16);
    tcpHeaderType.push_back_field("dstPort", 16);
    tcpHeaderType.push_back_field("seqNo", 32);
    tcpHeaderType.push_back_field("ackNo", 32);
    tcpHeaderType.push_back_field("dataOffset", 4);
    tcpHeaderType.push_back_field("res", 4);
    tcpHeaderType.push_back_field("flags", 8);
    tcpHeaderType.push_back_field("window", 16);
    tcpHeaderType.push_back_field("checksum", 16);
    tcpHeaderType.push_back_field("urgentPtr", 16);

    phv.push_back_header("ethernet", ethernetHeader, ethernetHeaderType);
    phv.push_back_header("ipv4", ipv4Header, ipv4HeaderType);
    phv.push_back_header("udp", udpHeader, udpHeaderType);
    phv.push_back_header("tcp", tcpHeader, tcpHeaderType);
  }

  virtual void SetUp() {
    ParseSwitchKeyBuilder ethernetKeyBuilder;
    ethernetKeyBuilder.push_back_field(ethernetHeader, 2); // ethertype
    ethernetParseState.set_key_builder(ethernetKeyBuilder);

    ParseSwitchKeyBuilder ipv4KeyBuilder;
    ipv4KeyBuilder.push_back_field(ipv4Header, 8); // protocol
    ipv4ParseState.set_key_builder(ipv4KeyBuilder);

    ethernetParseState.add_extract(ethernetHeader);
    ipv4ParseState.add_extract(ipv4Header);
    udpParseState.add_extract(udpHeader);
    tcpParseState.add_extract(tcpHeader);

    char ethernet_ipv4_key[2];
    ethernet_ipv4_key[0] = 0x08;
    ethernet_ipv4_key[1] = 0x00;
    ethernetParseState.add_switch_case(sizeof(ethernet_ipv4_key),
				       ethernet_ipv4_key, &ipv4ParseState);

    char ipv4_udp_key[1];
    ipv4_udp_key[0] = 17;
    ipv4ParseState.add_switch_case(sizeof(ipv4_udp_key), ipv4_udp_key,
				   &udpParseState);

    char ipv4_tcp_key[1];
    ipv4_tcp_key[0] = 6;
    ipv4ParseState.add_switch_case(sizeof(ipv4_tcp_key),
				   ipv4_tcp_key, &tcpParseState);

    parser.set_init_state(&ethernetParseState);
  }

  void get_ipv4_pkt(Packet *pkt, unsigned short *csum) {
    *pkt = Packet(
	0, 0, 0,
	PacketBuffer(256, (const char *) raw_tcp_pkt, sizeof(raw_tcp_pkt))
    );
    *csum = 0x3508; // big endian
  }

  // virtual void TearDown() {}
};

TEST_F(ChecksumTest, IPv4Checksum) {
  Packet packet;
  unsigned short csum;
  get_ipv4_pkt(&packet, &csum);
  parser.parse(&packet, &phv);

  Field &ipv4_checksum = phv.get_field(ipv4Header, 9);
  ASSERT_EQ(csum, ipv4_checksum.get_uint());

  ipv4_checksum.set(0);
  ASSERT_EQ((unsigned) 0, ipv4_checksum.get_uint());

  checksum::update_ipv4_csum(ipv4Header, 9, &phv);
  ASSERT_EQ(csum, ipv4_checksum.get_uint());
}
