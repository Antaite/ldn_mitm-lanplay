/* SPDX-License-Identifier: GPL-2.0-only */
#include "bsd_mitm.hpp"
#include "lanplay_service.hpp"
#include "debug.hpp"
#include "lanplay/lp_bridge.h"
#include "lanplay/vnet.h"
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include <algorithm>

namespace ams::mitm::bsd {

    namespace {

        /* Horizon's BSD values (FreeBSD). */
        constexpr s32 Af_Inet      = 2;
        constexpr s32 Sock_Dgram   = 2;
        constexpr s32 E_NetUnreach = 51;
        constexpr u32 Loopback     = 0x7F000001;

        struct Out2 { s32 ret; s32 err; };
        struct Out3 { s32 ret; s32 err; u32 len; };
        struct InFdFlags { s32 fd; s32 flags; };

        /* ---- which game fds are UDP (we only see the type at socket()) ---- */
        struct TypeEntry { u64 pid; s32 fd; };
        constexpr size_t MaxUdp = 64;
        constinit TypeEntry g_udp[MaxUdp] = {};
        constinit os::SdkMutex g_udp_lock;

        void TrackUdp(u64 pid, s32 fd) {
            std::scoped_lock lk(g_udp_lock);
            for (auto &e : g_udp) if (e.pid == 0) { e.pid = pid; e.fd = fd; return; }
        }
        bool IsUdp(u64 pid, s32 fd) {
            std::scoped_lock lk(g_udp_lock);
            for (auto &e : g_udp) if (e.pid == pid && e.fd == fd) return true;
            return false;
        }
        void Untrack(u64 pid, s32 fd) {
            std::scoped_lock lk(g_udp_lock);
            for (auto &e : g_udp) if (e.pid == pid && (fd < 0 || e.fd == fd)) e = {};
        }

        /* ---- first bsd:u session of a launch is a dummy that must not be intercepted ---- */
        struct PidEntry { u64 pid; bool dummy_skipped; u32 live; };
        constinit PidEntry g_pids[8] = {};
        constinit os::SdkMutex g_pid_lock;

        bool Active() {
            return ldn::lanplay::UseVirtualNetwork();
        }

        bool ParseAddr(const u8 *a, size_t len, u32 *ip, u16 *port) {
            if (len < 8 || a[1] != Af_Inet) return false;
            *port = static_cast<u16>(a[2] << 8 | a[3]);
            *ip   = static_cast<u32>(a[4]) << 24 | static_cast<u32>(a[5]) << 16 | static_cast<u32>(a[6]) << 8 | a[7];
            return true;
        }

        void WriteAddr(u8 *out, size_t cap, u32 ip, u16 port) {
            const u8 a[16] = { 16, Af_Inet, static_cast<u8>(port >> 8), static_cast<u8>(port),
                               static_cast<u8>(ip >> 24), static_cast<u8>(ip >> 16), static_cast<u8>(ip >> 8), static_cast<u8>(ip) };
            std::memcpy(out, a, std::min<size_t>(cap, sizeof(a)));
        }

        Result FwdAddrIn(Service *s, u32 cmd, s32 fd, const void *addr, size_t len, Out2 *o) {
            return serviceMitmDispatchInOut(s, cmd, fd, *o,
                .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
                .buffers = { { addr, len } });
        }

        Result FwdAddrOut(Service *s, u32 cmd, s32 fd, void *addr, size_t len, Out3 *o) {
            return serviceMitmDispatchInOut(s, cmd, fd, *o,
                .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                .buffers = { { addr, len } });
        }

        u16 QueryPort(Service *s, s32 fd) {
            u8 a[16] = {};
            Out3 o = { -1, 0, 0 };
            if (R_FAILED(FwdAddrOut(s, 16, fd, a, sizeof(a), std::addressof(o))) || o.ret != 0) return 0;
            u32 ip; u16 port;
            return ParseAddr(a, sizeof(a), std::addressof(ip), std::addressof(port)) ? port : 0;
        }

    }

    BsdMitmService::BsdMitmService(std::shared_ptr<::Service> &&s, const sm::MitmProcessInfo &c) : sf::MitmServiceImplBase(std::forward<std::shared_ptr<::Service>>(s), c) {
        std::scoped_lock lk(g_pid_lock);
        for (auto &e : g_pids) if (e.pid == this->Pid()) { e.live++; break; }
    }

    BsdMitmService::~BsdMitmService() {
        bool last = false;
        {
            std::scoped_lock lk(g_pid_lock);
            for (auto &e : g_pids) {
                if (e.pid == this->Pid() && e.live > 0 && --e.live == 0) { e = {}; last = true; break; }
            }
        }
        if (last) {
            lp_bridge_close_owner(this->Pid());
            Untrack(this->Pid(), -1);
            LogFormat("bsd: pid %" PRIu64 " closed its last session", this->Pid());
        }
    }

    bool BsdMitmService::ShouldMitm(const sm::MitmProcessInfo &c) {
        if (!ncm::IsApplicationId(c.program_id) || !Active()) {
            return false;
        }
        std::scoped_lock lk(g_pid_lock);
        PidEntry *e = nullptr;
        for (auto &p : g_pids) if (p.pid == c.process_id.value) { e = std::addressof(p); break; }
        if (e == nullptr) {
            for (auto &p : g_pids) if (p.pid == 0 || p.live == 0) { e = std::addressof(p); break; }
            if (e == nullptr) return false;
            *e = { c.process_id.value, false, 0 };
        }
        if (!e->dummy_skipped) {
            e->dummy_skipped = true;
            LogFormat("bsd: pid %" PRIu64 " %016" PRIx64 " first session passed through", c.process_id.value, c.program_id.value);
            return false;
        }
        LogFormat("bsd: pid %" PRIu64 " %016" PRIx64 " session intercepted", c.process_id.value, c.program_id.value);
        return true;
    }

    Result BsdMitmService::RegisterClient(sf::Out<u64> out_result, const LibraryConfigData &config, const sf::ClientProcessId &client_pid,
                                          u64 tmem_size, sf::CopyHandle &&transfer_memory) {
        AMS_UNUSED(client_pid);
        const struct { LibraryConfigData config; u64 pid_placeholder; u64 tmem_size; } in = { config, 0, tmem_size };
        u64 out = 0;
        const Result rc = serviceMitmDispatchInOut(m_forward_service.get(), 0, in, out,
            .in_send_pid = true,
            .in_num_handles = 1,
            .in_handles = { transfer_memory.GetOsHandle() },
            .override_pid = m_client_info.process_id.value);
        LogFormat("bsd: RegisterClient pid %" PRIu64 " rc=%x", this->Pid(), rc.GetValue());
        out_result.SetValue(out);
        return rc;
    }

    Result BsdMitmService::RegisterClientShared(sf::Out<u64> out_result, const LibraryConfigData &config, const sf::ClientProcessId &client_pid,
                                                u64 tmem_size) {
        AMS_UNUSED(client_pid);
        const struct { LibraryConfigData config; u64 pid_placeholder; u64 tmem_size; } in = { config, 0, tmem_size };
        u64 out = 0;
        const Result rc = serviceMitmDispatchInOut(m_forward_service.get(), 33, in, out,
            .in_send_pid = true,
            .override_pid = m_client_info.process_id.value);
        LogFormat("bsd: RegisterClientShared pid %" PRIu64 " rc=%x", this->Pid(), rc.GetValue());
        out_result.SetValue(out);
        return rc;
    }

    /* Make sure a UDP socket has a twin on the virtual network (binding it to an ephemeral port if needed). */
    bool BsdMitmService::EnsureBridged(s32 fd) {
        if (lp_bridge_get(this->Pid(), fd, nullptr)) return true;
        if (lp_bridge_start() != 0) return false;
        u16 port = QueryPort(m_forward_service.get(), fd);
        if (port == 0) {
            const u8 any[16] = { 16, Af_Inet };
            Out2 o = { -1, 0 };
            if (R_FAILED(FwdAddrIn(m_forward_service.get(), 13, fd, any, sizeof(any), std::addressof(o)))) return false;
            port = QueryPort(m_forward_service.get(), fd);
        }
        return port != 0 && lp_bridge_open(this->Pid(), fd, port, Loopback, 0) == 0;
    }

    Result BsdMitmService::Socket(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 domain, s32 type, s32 protocol) {
        const struct { s32 domain, type, protocol; } in = { domain, type, protocol };
        Out2 o = { -1, 0 };
        R_TRY(serviceMitmDispatchInOut(m_forward_service.get(), 2, in, o));
        if (o.ret >= 0 && domain == Af_Inet && (type & 0xF) == Sock_Dgram) {
            TrackUdp(this->Pid(), o.ret);
        }
        out_ret.SetValue(o.ret);
        out_errno.SetValue(o.err);
        R_SUCCEED();
    }

    Result BsdMitmService::Bind(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, const sf::InAutoSelectBuffer &addr) {
        u8 a[16] = {};
        const size_t len = std::min<size_t>(addr.GetSize(), sizeof(a));
        std::memcpy(a, addr.GetPointer(), len);
        u32 ip = 0; u16 port = 0;
        const bool udp = Active() && IsUdp(this->Pid(), fd) && ParseAddr(a, len, std::addressof(ip), std::addressof(port));
        const bool virt = udp && lp_bridge_is_virtual(ip, 0);
        if (virt) {
            std::memset(a + 4, 0, 4);   /* the real stack has no 10.13.x.x: bind to any instead */
        }

        Out2 o = { -1, 0 };
        if (virt) {
            R_TRY(FwdAddrIn(m_forward_service.get(), 13, fd, a, len, std::addressof(o)));
        } else {
            R_TRY(FwdAddrIn(m_forward_service.get(), 13, fd, addr.GetPointer(), addr.GetSize(), std::addressof(o)));
        }

        if (udp && o.ret == 0 && lp_bridge_start() == 0) {
            if (port == 0) port = QueryPort(m_forward_service.get(), fd);
            const u32 inject = (virt || ip == 0) ? Loopback : ip;
            if (port != 0) lp_bridge_open(this->Pid(), fd, port, inject, virt ? 1 : 0);
        }
        out_ret.SetValue(o.ret);
        out_errno.SetValue(o.err);
        R_SUCCEED();
    }

    Result BsdMitmService::SendTo(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, s32 flags,
                                  const sf::InAutoSelectBuffer &buffer, const sf::InAutoSelectBuffer &addr) {
        u32 ip; u16 port;
        if (Active() && IsUdp(this->Pid(), fd) && ParseAddr(addr.GetPointer(), addr.GetSize(), std::addressof(ip), std::addressof(port))
            && lp_bridge_is_virtual(ip, ldn::lanplay::IsSessionActive() ? 1 : 0)) {
            const long n = this->EnsureBridged(fd) ? lp_bridge_sendto(this->Pid(), fd, buffer.GetPointer(), buffer.GetSize(), ip, port) : -1;
            out_ret.SetValue(n >= 0 ? static_cast<s32>(n) : -1);
            out_errno.SetValue(n >= 0 ? 0 : E_NetUnreach);
            R_SUCCEED();
        }
        const InFdFlags in = { fd, flags };
        Out2 o = { -1, 0 };
        R_TRY(serviceMitmDispatchInOut(m_forward_service.get(), 11, in, o,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In, SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
            .buffers = { { buffer.GetPointer(), buffer.GetSize() }, { addr.GetPointer(), addr.GetSize() } }));
        out_ret.SetValue(o.ret);
        out_errno.SetValue(o.err);
        R_SUCCEED();
    }

    Result BsdMitmService::Send(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, s32 flags, const sf::InAutoSelectBuffer &buffer) {
        lp_bridge_sock_t s;
        if (lp_bridge_get(this->Pid(), fd, std::addressof(s)) && s.has_peer) {
            const long n = lp_bridge_sendto(this->Pid(), fd, buffer.GetPointer(), buffer.GetSize(), s.peer_ip, s.peer_port);
            out_ret.SetValue(n >= 0 ? static_cast<s32>(n) : -1);
            out_errno.SetValue(n >= 0 ? 0 : E_NetUnreach);
            R_SUCCEED();
        }
        const InFdFlags in = { fd, flags };
        Out2 o = { -1, 0 };
        R_TRY(serviceMitmDispatchInOut(m_forward_service.get(), 10, in, o,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_In },
            .buffers = { { buffer.GetPointer(), buffer.GetSize() } }));
        out_ret.SetValue(o.ret);
        out_errno.SetValue(o.err);
        R_SUCCEED();
    }

    Result BsdMitmService::Connect(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, const sf::InAutoSelectBuffer &addr) {
        u32 ip; u16 port;
        const bool parsed = ParseAddr(addr.GetPointer(), addr.GetSize(), std::addressof(ip), std::addressof(port));
        if (Active() && parsed && lp_bridge_is_virtual(ip, 0)) {
            if (IsUdp(this->Pid(), fd) && this->EnsureBridged(fd)) {
                lp_bridge_set_peer(this->Pid(), fd, 1, ip, port);
                out_ret.SetValue(0);
                out_errno.SetValue(0);
            } else {
                LogFormat("bsd: connect to %x:%u not bridged (TCP or no twin)", ip, port);
                out_ret.SetValue(-1);
                out_errno.SetValue(E_NetUnreach);
            }
            R_SUCCEED();
        }
        lp_bridge_set_peer(this->Pid(), fd, 0, 0, 0);
        Out2 o = { -1, 0 };
        R_TRY(FwdAddrIn(m_forward_service.get(), 14, fd, addr.GetPointer(), addr.GetSize(), std::addressof(o)));
        out_ret.SetValue(o.ret);
        out_errno.SetValue(o.err);
        R_SUCCEED();
    }

    /* recv/recvfrom on a bridged socket: read with room for our header, strip it, report the real sender. */
    Result BsdMitmService::RecvCommon(s32 fd, s32 flags, u8 *buf, size_t len, u8 *addr, size_t addr_len,
                                      s32 *ret, s32 *err, u32 *addrlen) {
        const InFdFlags in = { fd, flags };
        Out3 o = { -1, 0, 0 };
        /* The bsd manager runs on one thread, so one static buffer serves every call;
         * only an unusually large read falls back to the heap. */
        alignas(8) static u8 s_buf[LP_BRIDGE_HDR + LP_BRIDGE_MAX_DGRAM];
        const size_t want = std::min<size_t>(len, 0x10000) + LP_BRIDGE_HDR;
        u8 *tmp = want <= sizeof(s_buf) ? s_buf : static_cast<u8 *>(std::malloc(want));
        if (tmp == nullptr) {
            R_TRY(serviceMitmDispatchInOut(m_forward_service.get(), 9, in, o,
                .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out, SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                .buffers = { { buf, len }, { addr, addr_len } }));
            *ret = o.ret; *err = o.err; *addrlen = o.len;
            R_SUCCEED();
        }
        ON_SCOPE_EXIT { if (tmp != s_buf) std::free(tmp); };

        u8 from[16] = {};
        R_TRY(serviceMitmDispatchInOut(m_forward_service.get(), 9, in, o,
            .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out, SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
            .buffers = { { tmp, want }, { from, sizeof(from) } }));
        *err = o.err;
        if (o.ret < 0) {
            *ret = o.ret; *addrlen = 0;
            R_SUCCEED();
        }

        u32 vip; u16 vport, fport = static_cast<u16>(from[2] << 8 | from[3]);
        if (fport == lp_bridge_inject_port() && lp_bridge_parse(tmp, static_cast<size_t>(o.ret), std::addressof(vip), std::addressof(vport))) {
            const size_t n = std::min<size_t>(static_cast<size_t>(o.ret) - LP_BRIDGE_HDR, len);
            std::memcpy(buf, tmp + LP_BRIDGE_HDR, n);
            if (addr && addr_len) WriteAddr(addr, addr_len, vip, vport);
            *ret = static_cast<s32>(n); *addrlen = 16;
        } else {
            const size_t n = std::min<size_t>(static_cast<size_t>(o.ret), len);
            std::memcpy(buf, tmp, n);
            if (addr && addr_len) std::memcpy(addr, from, std::min<size_t>(addr_len, sizeof(from)));
            *ret = static_cast<s32>(n); *addrlen = o.len;
        }
        R_SUCCEED();
    }

    Result BsdMitmService::RecvFrom(sf::Out<s32> out_ret, sf::Out<s32> out_errno, sf::Out<u32> out_addrlen, s32 fd, s32 flags,
                                    sf::OutAutoSelectBuffer buffer, sf::OutAutoSelectBuffer addr) {
        s32 ret = -1, err = 0; u32 alen = 0;
        if (lp_bridge_get(this->Pid(), fd, nullptr)) {
            R_TRY(this->RecvCommon(fd, flags, buffer.GetPointer(), buffer.GetSize(), addr.GetPointer(), addr.GetSize(), &ret, &err, &alen));
        } else {
            const InFdFlags in = { fd, flags };
            Out3 o = { -1, 0, 0 };
            R_TRY(serviceMitmDispatchInOut(m_forward_service.get(), 9, in, o,
                .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out, SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                .buffers = { { buffer.GetPointer(), buffer.GetSize() }, { addr.GetPointer(), addr.GetSize() } }));
            ret = o.ret; err = o.err; alen = o.len;
        }
        out_ret.SetValue(ret);
        out_errno.SetValue(err);
        out_addrlen.SetValue(alen);
        R_SUCCEED();
    }

    Result BsdMitmService::Recv(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd, s32 flags, sf::OutAutoSelectBuffer buffer) {
        s32 ret = -1, err = 0; u32 alen = 0;
        if (lp_bridge_get(this->Pid(), fd, nullptr)) {
            /* Uses recvfrom underneath: the sender port tells our datagrams apart. */
            R_TRY(this->RecvCommon(fd, flags, buffer.GetPointer(), buffer.GetSize(), nullptr, 0, &ret, &err, &alen));
        } else {
            const InFdFlags in = { fd, flags };
            Out2 o = { -1, 0 };
            R_TRY(serviceMitmDispatchInOut(m_forward_service.get(), 8, in, o,
                .buffer_attrs = { SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out },
                .buffers = { { buffer.GetPointer(), buffer.GetSize() } }));
            ret = o.ret; err = o.err;
        }
        out_ret.SetValue(ret);
        out_errno.SetValue(err);
        R_SUCCEED();
    }

    Result BsdMitmService::GetSockName(sf::Out<s32> out_ret, sf::Out<s32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer addr) {
        Out3 o = { -1, 0, 0 };
        R_TRY(FwdAddrOut(m_forward_service.get(), 16, fd, addr.GetPointer(), addr.GetSize(), std::addressof(o)));
        lp_bridge_sock_t s;
        if (o.ret == 0 && lp_bridge_get(this->Pid(), fd, std::addressof(s)) && s.virt_bound) {
            WriteAddr(addr.GetPointer(), addr.GetSize(), vnet_get_ip(), s.port);   /* the game bound its virtual IP */
            o.len = 16;
        }
        out_ret.SetValue(o.ret);
        out_errno.SetValue(o.err);
        out_addrlen.SetValue(o.len);
        R_SUCCEED();
    }

    Result BsdMitmService::GetPeerName(sf::Out<s32> out_ret, sf::Out<s32> out_errno, sf::Out<u32> out_addrlen, s32 fd, sf::OutAutoSelectBuffer addr) {
        lp_bridge_sock_t s;
        if (lp_bridge_get(this->Pid(), fd, std::addressof(s)) && s.has_peer) {
            WriteAddr(addr.GetPointer(), addr.GetSize(), s.peer_ip, s.peer_port);
            out_ret.SetValue(0);
            out_errno.SetValue(0);
            out_addrlen.SetValue(16);
            R_SUCCEED();
        }
        Out3 o = { -1, 0, 0 };
        R_TRY(FwdAddrOut(m_forward_service.get(), 15, fd, addr.GetPointer(), addr.GetSize(), std::addressof(o)));
        out_ret.SetValue(o.ret);
        out_errno.SetValue(o.err);
        out_addrlen.SetValue(o.len);
        R_SUCCEED();
    }

    Result BsdMitmService::Close(sf::Out<s32> out_ret, sf::Out<s32> out_errno, s32 fd) {
        Out2 o = { -1, 0 };
        R_TRY(serviceMitmDispatchInOut(m_forward_service.get(), 26, fd, o));
        if (o.ret == 0) {
            lp_bridge_close(this->Pid(), fd);
            Untrack(this->Pid(), fd);
        }
        out_ret.SetValue(o.ret);
        out_errno.SetValue(o.err);
        R_SUCCEED();
    }

}
