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

namespace ams::mitm::bsd {
    /* nn::socket LibraryConfigData: forwarded untouched. */
    struct LibraryConfigData { u8 raw[0x20]; };
    static_assert(sizeof(LibraryConfigData) == 0x20);
}

/* RegisterClient carries the client's pid and a transfer-memory handle: a plain
 * framework forward loses both (the game aborts in nn::socket::Initialize), so it
 * is re-sent with Atmosphere's pid override. StartMonitoring (1) must stay
 * undeclared: its raw-forward is the only shape that matches the real client. */
#define AMS_LP_BSD_MITM(C, H)                                                                                                    \
    AMS_SF_METHOD_INFO(C, H, 0,  Result, RegisterClient, (ams::sf::Out<u64> out_result, const ams::mitm::bsd::LibraryConfigData &config, \
                                                          const ams::sf::ClientProcessId &client_pid, u64 tmem_size,               \
                                                          ams::sf::CopyHandle &&transfer_memory),                                  \
                                                         (out_result, config, client_pid, tmem_size, std::move(transfer_memory)))   \
    AMS_SF_METHOD_INFO(C, H, 33, Result, RegisterClientShared, (ams::sf::Out<u64> out_result, const ams::mitm::bsd::LibraryConfigData &config, \
                                                          const ams::sf::ClientProcessId &client_pid, u64 tmem_size),              \
                                                         (out_result, config, client_pid, tmem_size), ams::hos::Version_10_0_0)     \
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

            Result RegisterClient(sf::Out<u64> out_result, const LibraryConfigData &config, const sf::ClientProcessId &client_pid,
                                  u64 tmem_size, sf::CopyHandle &&transfer_memory);
            Result RegisterClientShared(sf::Out<u64> out_result, const LibraryConfigData &config, const sf::ClientProcessId &client_pid,
                                        u64 tmem_size);

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

    /* The game's bsd:u session goes through us (decided when the game started). */
    bool IsIntercepted(u64 pid);

}
