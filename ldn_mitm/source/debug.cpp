/*
 * Copyright (c) 2018 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "debug.hpp"

const size_t TlsBackupSize = 0x100;

/* Off by default: switch it on from the overlay when something needs looking at.
 * Logging must never take the system module down: any SD failure (card busy,
 * sleep/wake, full card) just turns it off again. */
static std::atomic_bool g_logging_enabled = false;
#define BACKUP_TLS() u8 _tls_backup[TlsBackupSize];memcpy(_tls_backup, armGetTls(), TlsBackupSize);
#define RESTORE_TLS() memcpy(armGetTls(), _tls_backup, TlsBackupSize);

#define MIN(a, b) (((a) > (b)) ? (b) : (a))

ams::Result SetLogging(u32 enabled)
{
    g_logging_enabled = enabled != 0;

    if (g_logging_enabled) {
        const ams::Result rc = ams::log::Initialize();
        if (R_FAILED(rc)) {
            g_logging_enabled = false;
            return rc;
        }
    }

    R_SUCCEED();
}

ams::Result GetLogging(u32 *enabled)
{
    *enabled = g_logging_enabled;

    R_SUCCEED();
}

namespace ams::log
{
    namespace {
        constexpr const char LogFilePath[] = "sdmc:/ldn_mitm.log";
        constexpr s64 MaxLogSize = 1024 * 1024;
        fs::FileHandle LogFile;
        s64 LogOffset;

        os::Mutex g_file_log_lock(true);

        /* Write at the current offset; on failure logging switches itself off. */
        bool Put(const void *buf, size_t len, fs::WriteOption opt)
        {
            if (len == 0) {
                return true;
            }
            if (R_FAILED(fs::WriteFile(LogFile, LogOffset, buf, len, opt))) {
                g_logging_enabled = false;
                return false;
            }
            LogOffset += static_cast<s64>(len);
            return true;
        }

        bool Open()
        {
            if (LogOffset > MaxLogSize) {
                static_cast<void>(fs::DeleteFile(LogFilePath));
                if (R_FAILED(fs::CreateFile(LogFilePath, 0))) {
                    g_logging_enabled = false;
                    return false;
                }
                LogOffset = 0;
            }
            if (R_FAILED(fs::OpenFile(&LogFile, LogFilePath, fs::OpenMode_Write | fs::OpenMode_AllowAppend))) {
                g_logging_enabled = false;
                return false;
            }
            return true;
        }
    }

    Result Initialize()
    {
        if (g_logging_enabled) {
            std::scoped_lock lk(g_file_log_lock);
            bool has_file;
            R_TRY(fs::HasFile(&has_file, LogFilePath));
            if (!has_file)
            {
                R_TRY(fs::CreateFile(LogFilePath, 0));
            }

            R_TRY(fs::OpenFile(&LogFile, LogFilePath, fs::OpenMode_Write | fs::OpenMode_AllowAppend));
            const Result rc = GetFileSize(&LogOffset, LogFile);
            fs::CloseFile(LogFile);
            R_TRY(rc);

            if (LogOffset > MaxLogSize) {
                R_TRY(fs::DeleteFile(LogFilePath));
                R_TRY(fs::CreateFile(LogFilePath, 0));
                LogOffset = 0;
            }
        }

        R_SUCCEED();
    }

    void Finalize()
    {
    }

    namespace {
        bool LogPrefix()
        {
            char buf[0x100];
            auto thread = os::GetCurrentThread();
            auto ts = os::GetSystemTick().ToTimeSpan();

            auto len = util::TSNPrintf(buf, sizeof(buf), "[ts: %6lums t: (%lu) %-22s p: %d/%d] ",
                                       ts.GetMilliSeconds(),
                                       os::GetThreadId(thread),
                                       os::GetThreadNamePointer(thread),
                                       os::GetThreadPriority(thread) + 28,
                                       os::GetThreadCurrentPriority(thread) + 28);
            return Put(buf, static_cast<size_t>(MIN(len, static_cast<int>(sizeof(buf) - 1))), fs::WriteOption::None);
        }
    }

    void LogStr(const char *fmt, std::va_list args)
    {
        if (!g_logging_enabled) {
            return;
        }
        std::scoped_lock lk(g_file_log_lock);
        if (!g_logging_enabled) {
            return;
        }
        BACKUP_TLS();
        if (Open()) {
            if (LogPrefix()) {
                char buf[0x100];
                int len = util::TVSNPrintf(buf, sizeof(buf), fmt, args);
                if (len > static_cast<int>(sizeof(buf) - 1)) {
                    len = sizeof(buf) - 1;
                }
                if (len > 0) {
                    Put(buf, static_cast<size_t>(len), fs::WriteOption::Flush);
                }
            }
            fs::CloseFile(LogFile);
        }
        RESTORE_TLS();
    }

    void LogHexImpl(const void *data, int size)
    {
        if (!g_logging_enabled) {
            return;
        }
        LogFormatImpl("Bin Log: %d (%p)\n", size, data);
        std::scoped_lock lk(g_file_log_lock);
        if (!g_logging_enabled) {
            return;
        }
        const u8 *dat = static_cast<const u8 *>(data);
        char buf[0x40];
        BACKUP_TLS();
        if (Open()) {
            for (int i = 0; i < size; i += 16)
            {
                const int s = MIN(size - i, 16);
                int n = 0;
                for (int j = 0; j < s; j++)
                {
                    n += util::TSNPrintf(buf + n, sizeof(buf) - n, "%02x", dat[i + j]);
                }
                buf[n++] = '\n';
                if (!Put(buf, static_cast<size_t>(n), fs::WriteOption::Flush)) {
                    break;
                }
            }
            fs::CloseFile(LogFile);
        }
        RESTORE_TLS();
    }

    void LogFormatImpl(const char *fmt, ...)
    {
        std::va_list args;
        va_start(args, fmt);
        LogStr(fmt, args);
        va_end(args);
    }
}
