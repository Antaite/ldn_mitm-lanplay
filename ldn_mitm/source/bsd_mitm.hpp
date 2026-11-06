/*
 * bsd:u mitm for games in internet mode: carries a game's own UDP traffic to
 * peers on the lan-play network (10.13.0.0/16) through lanplay/lp_bridge.
 *
 * Only the commands that touch addresses are declared; everything else is
 * forwarded untouched by the framework. Every reply starts with
 * { s32 ret; s32 errno; } (libnx bsd.c), so ret is always declared first.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#pragma once
#include <stratosphere.hpp>

#define AMS_LP_BSD_MITM(C, H)                                                                                                    \
    AMS_SF_METHOD_INFO(C, H, 2,  Result, Socket,      (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, s32 domain, s32 type, s32 protocol), \
                                                      (out_ret, out_errno, domain, type, protocol))                              \
    AMS_SF_METHOD_INFO(C, H, 8,  Result, Recv,        (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, s32 fd, s32 flags,             \
                                                       ams::sf::OutAutoSelectBuffer buffer),                                     \
                                                      (out_ret, out_errno, fd, flags, buffer))                                   \
    AMS_SF_METHOD_INFO(C, H, 9,  Result, RecvFrom,    (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, ams::sf::Out<u32> out_addrlen, \
                                                       s32 fd, s32 flags, ams::sf::OutAutoSelectBuffer buffer,                   \
                                                       ams::sf::OutAutoSelectBuffer addr),                                       \
                                                      (out_ret, out_errno, out_addrlen, fd, flags, buffer, addr))                \
    AMS_SF_METHOD_INFO(C, H, 10, Result, Send,        (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, s32 fd, s32 flags,             \
                                                       const ams::sf::InAutoSelectBuffer &buffer),                               \
                                                      (out_ret, out_errno, fd, flags, buffer))                                   \
    AMS_SF_METHOD_INFO(C, H, 11, Result, SendTo,      (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, s32 fd, s32 flags,             \
                                                       const ams::sf::InAutoSelectBuffer &buffer, const ams::sf::InAutoSelectBuffer &addr), \
                                                      (out_ret, out_errno, fd, flags, buffer, addr))                             \
    AMS_SF_METHOD_INFO(C, H, 13, Result, Bind,        (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, s32 fd,                        \
                                                       const ams::sf::InAutoSelectBuffer &addr),                                 \
                                                      (out_ret, out_errno, fd, addr))                                            \
    AMS_SF_METHOD_INFO(C, H, 14, Result, Connect,     (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, s32 fd,                        \
                                                       const ams::sf::InAutoSelectBuffer &addr),                                 \
                                                      (out_ret, out_errno, fd, addr))                                            \
    AMS_SF_METHOD_INFO(C, H, 15, Result, GetPeerName, (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, ams::sf::Out<u32> out_addrlen, \
                                                       s32 fd, ams::sf::OutAutoSelectBuffer addr),                               \
                                                      (out_ret, out_errno, out_addrlen, fd, addr))                               \
    AMS_SF_METHOD_INFO(C, H, 16, Result, GetSockName, (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, ams::sf::Out<u32> out_addrlen, \
                                                       s32 fd, ams::sf::OutAutoSelectBuffer addr),                               \
                                                      (out_ret, out_errno, out_addrlen, fd, addr))                               \
    AMS_SF_METHOD_INFO(C, H, 26, Result, Close,       (ams::sf::Out<s32> out_ret, ams::sf::Out<s32> out_errno, s32 fd),                       \
                                                      (out_ret, out_errno, fd))

AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::bsd, IBsdMitm, AMS_LP_BSD_MITM, 0x4C504253)

namespace ams::mitm::bsd {

    class BsdMitmService : public sf::MitmServiceImplBase {
        public:
            BsdMitmService(std::shared_ptr<::Service> &&s, const sm::MitmProcessInfo &c);
            ~BsdMitmService();

            static bool ShouldMitm(const sm::MitmProcessInfo &client_info);

            Result Socket(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 domain, s32 type, s32 protocol);
            Result Recv(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, s32 flags, sf::OutAutoSelectBuffer buffer);
            Result RecvFrom(sf::Out<s32> out_ret, sf::Out<s32> out_errno, sf::Out<u32> out_addrlen, s32 fd, s32 flags,
                            sf::OutAutoSelectBuffer buffer, sf::OutAutoSelectBuffer addr);
            Result Send(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, s32 flags, const sf::InAutoSelectBuffer &buffer);
            Result SendTo(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, s32 flags,
                          const sf::InAutoSelectBuffer &buffer, const sf::InAutoSelectBuffer &addr);
            Result Bind(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, const sf::InAutoSelectBuffer &addr);
            Result Connect(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, const sf::InAutoSelectBuffer &addr);
            Result GetPeerName(sf::Out<s32> out_ret, sf::Out<s32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer addr);
            Result GetSockName(sf::Out<s32> out_ret, sf::Out<s32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer addr);
            Result Close(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd);
        private:
            u64 Pid() const { return m_client_info.process_id.value; }
            bool EnsureBridged(s32 fd);
            Result RecvCommon(s32 fd, s32 flags, u8 *buf, size_t len, u8 *addr, size_t addr_len,
                              s32 *ret, s32 *err, u32 *addrlen);
    };
    static_assert(ams::mitm::bsd::IsIBsdMitm<BsdMitmService>);

}
