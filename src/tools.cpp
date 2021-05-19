// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "tools.hpp"

#if !defined(_WIN32)
    #include <pthread.h>
    #include <unistd.h>
#endif // _WIN32

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif __MACH__
    #include <mach/mach_time.h>
    #include <mach/mach.h>
    #include <mach/clock.h>

    extern mach_port_t clock_port;
#else
    #include <time.h>
    #include <sys/time.h>
#endif

#include <arm_acle.h>
#include <arm_neon.h>

#include <string.h>

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>


//------------------------------------------------------------------------------
// Operating System Check

#if defined(__SVR4) && defined(__sun)
# define CAT_OS_SOLARIS

#elif defined(__APPLE__) && (defined(__MACH__) || defined(__DARWIN__))
# define CAT_OS_OSX
# define CAT_OS_APPLE

#elif defined(__APPLE__) && defined(TARGET_OS_IPHONE)
# define CAT_OS_IPHONE
# define CAT_OS_APPLE

#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__)
# define CAT_OS_BSD

#elif defined(__linux__) || defined(__unix__) || defined(__unix)
# define CAT_OS_LINUX

#ifdef __ANDROID__
# define CAT_OS_ANDROID
#endif

#elif defined(_WIN32_WCE)
# define CAT_OS_WINDOWS_CE
# define CAT_OS_WINDOWS /* Also defined */

#elif defined(_WIN32)
# define CAT_OS_WINDOWS

#elif defined(_XBOX) || defined(_X360)
# define CAT_OS_XBOX

#elif defined(_PS3) || defined(__PS3__) || defined(SN_TARGET_PS3)
# define CAT_OS_PS3

#elif defined(_PS4) || defined(__PS4__) || defined(SN_TARGET_PS4)
# define CAT_OS_PS4

#elif defined(__OS2__)
# define CAT_OS_OS2

#elif defined(__APPLE__)
# define CAT_OS_APPLE

#elif defined(_aix) || defined(aix)
# define CAT_OS_AIX

#elif defined(HPUX)
# define CAT_OS_HPUX

#elif defined(IRIX)
# define CAT_OS_IRIX

#else
# define CAT_OS_UNKNOWN
#endif

#if defined(CAT_OS_LINUX) || defined(CAT_OS_OSX)
# include <sys/mman.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <errno.h>
#endif

#if defined(CAT_OS_WINDOWS)
# include <windows.h>
#elif defined(CAT_OS_LINUX) || defined(CAT_OS_AIX) || defined(CAT_OS_SOLARIS) || defined(CAT_OS_IRIX)
# include <unistd.h>
#elif defined(CAT_OS_OSX) || defined(CAT_OS_BSD)
# include <sys/sysctl.h>
# include <unistd.h>
#elif defined(CAT_OS_HPUX)
# include <sys/mpctl.h>
#endif

namespace lora {


//------------------------------------------------------------------------------
// Tools

#ifdef _WIN32
static double PerfFrequencyInverse = 0.;

static void InitPerfFrequencyInverse()
{
    LARGE_INTEGER freq = {};
    if (!::QueryPerformanceFrequency(&freq) || freq.QuadPart == 0)
        return;
    PerfFrequencyInverse = 1000000. / (double)freq.QuadPart;
}
#elif __MACH__
static bool m_clock_serv_init = false;
static clock_serv_t m_clock_serv = 0;

static void InitClockServ()
{
    m_clock_serv_init = true;
    host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &m_clock_serv);
}
#endif // _WIN32

uint64_t GetTimeUsec()
{
#ifdef _WIN32
    LARGE_INTEGER timeStamp = {};
    if (!::QueryPerformanceCounter(&timeStamp))
        return 0;
    if (PerfFrequencyInverse == 0.)
        InitPerfFrequencyInverse();
    return (uint64_t)(PerfFrequencyInverse * timeStamp.QuadPart);
#elif __MACH__
    if (!m_clock_serv_init)
        InitClockServ();

    mach_timespec_t tv;
    clock_get_time(m_clock_serv, &tv);

    return 1000000 * tv.tv_sec + tv.tv_nsec / 1000;
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return 1000000 * tv.tv_sec + tv.tv_usec;
#endif
}

uint64_t GetTimeMsec()
{
    return GetTimeUsec() / 1000;
}

#if __ARM_ARCH >= 8

uint32_t FastCrc32(const void* vdata, int bytes)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>( vdata );

    uint32_t crc = UINT32_C(0xffffffff);

    while (bytes >= 8) {
        crc = __crc32cd(crc, ReadU64_LE(data));
        data += 8;
        bytes -= 8;
    }

    if (bytes & 4) {
        crc = __crc32cw(crc, ReadU32_LE(data));
        data += 4;
    }

    if (bytes & 2) {
        crc = __crc32ch(crc, ReadU16_LE(data));
        data += 2;
    }

    if (bytes & 1) {
        crc = __crc32cb(crc, *data);
    }

    return ~crc;
}

#else // Fallback when processor cannot do it for us:

static const uint32_t CRC32_LUT[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 
    0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 
    0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59, 
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 
    0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01, 
    0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 
    0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad, 
    0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 
    0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79, 
    0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 
    0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777, 0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45, 
    0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9, 
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d, 
};

uint32_t FastCrc32(const void* vdata, int bytes)
{
    const uint8_t* data = reinterpret_cast<const uint8_t*>( vdata );

    uint32_t crc = UINT32_C(0xffffffff);

    for (int i = 0; i < bytes; ++i) {
        crc = (crc >> 8) ^ CRC32_LUT[static_cast<uint8_t>(crc ^ data[i])];
    }

    return ~crc;
}

#endif


//------------------------------------------------------------------------------
// MappedFile

static uint32_t GetAllocationGranularity()
{
    uint32_t alloc_gran = 0;
#if defined(CAT_OS_WINDOWS)
    SYSTEM_INFO sys_info;
    ::GetSystemInfo(&sys_info);
    alloc_gran = sys_info.dwAllocationGranularity;
#elif defined(CAT_OS_OSX) || defined(CAT_OS_BSD)
    alloc_gran = (uint32_t)getpagesize();
#else
    alloc_gran = (uint32_t)sysconf(_SC_PAGE_SIZE);
#endif
    return alloc_gran > 0 ? alloc_gran : 32;
}

MappedFile::MappedFile()
{
    Length = 0;
#if defined(CAT_OS_WINDOWS)
    File = INVALID_HANDLE_VALUE;
#else
    File = -1;
#endif
}

MappedFile::~MappedFile()
{
    Close();
}

bool MappedFile::OpenRead(
    const char* path,
    bool read_ahead,
    bool no_cache)
{
    Close();

    ReadOnly = true;

#if defined(CAT_OS_WINDOWS)

    (void)no_cache;
    const uint32_t access_pattern = !read_ahead ? FILE_FLAG_RANDOM_ACCESS : FILE_FLAG_SEQUENTIAL_SCAN;

    File = ::CreateFileA(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        0,
        OPEN_EXISTING,
        access_pattern,
        0);

    if (File == INVALID_HANDLE_VALUE) {
        return false;
    }

    const BOOL getSuccess = ::GetFileSizeEx(File, (LARGE_INTEGER*)&Length);

    if (getSuccess != TRUE) {
        return false;
    }

#else
    File = open(path, O_RDONLY, (mode_t)0444);

    if (File == -1) {
        return false;
    }

    Length = lseek(File, 0, SEEK_END);

    if (Length <= 0) {
        return false;
    }

    (void)read_ahead;
#ifdef F_RDAHEAD
    if (read_ahead) {
        fcntl(File, F_RDAHEAD, 1);
    }
#endif

    (void)no_cache;
#ifdef F_NOCACHE
    if (no_cache) {
        fcntl(File, F_NOCACHE, 1);
    }
#endif

#endif

    return true;
}

bool MappedFile::OpenWrite(
    const char* path,
    uint64_t size)
{
    Close();

    ReadOnly = false;
    Length = 0;

#if defined(CAT_OS_WINDOWS)

    const uint32_t access_pattern = FILE_FLAG_SEQUENTIAL_SCAN;

    File = ::CreateFileA(
        path,
        GENERIC_WRITE|GENERIC_READ,
        FILE_SHARE_WRITE,
        0,
        CREATE_ALWAYS,
        access_pattern,
        0);

#else

    File = open(path, O_RDWR|O_CREAT|O_TRUNC, (mode_t)0666);

#endif

    return Resize(size);
}

bool MappedFile::Resize(uint64_t size)
{
    Length = size;

#if defined(CAT_OS_WINDOWS)

    if (File == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Set file size
    BOOL setSuccess = ::SetFilePointerEx(
        File,
        *(LARGE_INTEGER*)&Length,
        0,
        FILE_BEGIN);

    if (setSuccess != TRUE) {
        return false;
    }

    if (!::SetEndOfFile(File)) {
        return false;
    }

#else

    if (File == -1) {
        return false;
    }

    const int truncateResult = ftruncate(File, (off_t)size);

    if (0 != truncateResult) {
        return false;
    }

#if 0
    const int seekResult = lseek(File, size - 1, SEEK_SET);

    if (-1 == seekResult) {
        return false;
    }

    const int writeResult = write(File, "", 1);

    if (1 != writeResult) {
        return false;
    }
#endif

#endif

    return true;
}

void MappedFile::Close()
{
#if defined(CAT_OS_WINDOWS)

    if (File != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(File);
        File = INVALID_HANDLE_VALUE;
    }

#else

    if (File != -1)
    {
        close(File);
        File = -1;
    }

#endif

    Length = 0;
}


//------------------------------------------------------------------------------
// MappedView

MappedView::MappedView()
{
    Data = 0;
    Length = 0;
    Offset = 0;

#if defined(CAT_OS_WINDOWS)
    Map = 0;
#else
    Map = MAP_FAILED;
#endif
}

MappedView::~MappedView()
{
    Close();
}

bool MappedView::Open(MappedFile *file)
{
    Close();

    if (!file || !file->IsValid()) {
        return false;
    }

    File = file;

    return true;
}

uint8_t* MappedView::MapView(uint64_t offset, uint32_t length)
{
    Close();

    if (length == 0) {
        length = static_cast<uint32_t>( File->Length );
    }

    if (offset)
    {
        const uint32_t granularity = GetAllocationGranularity();

        // Bring offset back to the previous allocation granularity
        const uint32_t mask = granularity - 1;
        const uint32_t masked = static_cast<uint32_t>(offset) & mask;

        offset -= masked;
        length += masked;
    }

#if defined(CAT_OS_WINDOWS)

    const uint32_t protect = File->ReadOnly ? PAGE_READONLY : PAGE_READWRITE;

    Map = ::CreateFileMappingA(
        File->File,
        nullptr,
        protect,
        0, // no max size
        0, // no min size
        nullptr);

    if (!Map) {
        return nullptr;
    }

    uint32_t flags = FILE_MAP_READ;

    if (!File->ReadOnly) {
        flags |= FILE_MAP_WRITE;
    }

    Data = (uint8_t*)::MapViewOfFile(
        Map,
        flags,
        (uint32_t)(offset >> 32),
        (uint32_t)offset, length);

    if (!Data) {
        return nullptr;
    }

#else

    int prot = PROT_READ;

    if (!File->ReadOnly) {
        prot |= PROT_WRITE;
    }

    Map = mmap(
        0,
        length,
        prot,
        MAP_SHARED,
        File->File,
        offset);

    if (Map == MAP_FAILED) {
        return 0;
    }

    Data = reinterpret_cast<uint8_t*>( Map );

#endif

    Offset = offset;
    Length = length;

    return Data;
}

void MappedView::Close()
{
#if defined(CAT_OS_WINDOWS)

    if (Data)
    {
        ::UnmapViewOfFile(Data);
        Data = 0;
    }

    if (Map)
    {
        ::CloseHandle(Map);
        Map = 0;
    }

#else

    if (Map != MAP_FAILED)
    {
        munmap(Map, Length);
        Map = MAP_FAILED;
    }

    Data = 0;

#endif

    Length = 0;
    Offset = 0;
}


//------------------------------------------------------------------------------
// MappedReadOnlySmallFile

bool MappedReadOnlySmallFile::Read(const char* path)
{
    Close();

    if (!File.OpenRead(path)) {
        return false;
    }
    if (!View.Open(&File)) {
        return false;
    }
    if (!View.MapView(0, (uint32_t)File.Length)) {
        return false;
    }

    return true;
}

void MappedReadOnlySmallFile::Close()
{
    View.Close();
    File.Close();
}


//------------------------------------------------------------------------------
// File Helpers

/// Write the provided buffer to the file at the given path
bool WriteBufferToFile(const char* path, const void* data, uint64_t bytes)
{
    MappedFile file;
    MappedView view;

    if (!file.OpenWrite(path, bytes)) {
        return false;
    }
    if (!view.Open(&file)) {
        return false;    
    }
    if (!view.MapView(0, (uint32_t)file.Length)) {
        return false;
    }

    memcpy(view.Data, data, (size_t)bytes);

    view.Close();
    file.Close();

    return true;
}


//------------------------------------------------------------------------------
// Logging

static void AtExitWrapper()
{
    spdlog::info("Terminated");
    spdlog::shutdown();
}

void SetupAsyncDiskLog(const std::string& filename, bool print_debug_logs)
{
    spdlog::init_thread_pool(8192, 1);
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        filename,
        4*1024*1024,
        3);
    std::vector<spdlog::sink_ptr> sinks {
        stdout_sink,
        rotating_sink
    };
    auto logger = std::make_shared<spdlog::async_logger>(
        filename,
        sinks.begin(), sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::overrun_oldest);
        //spdlog::async_overflow_policy::block);
    spdlog::register_logger(logger);

    spdlog::set_default_logger(logger);

    spdlog::set_pattern("[%H:%M:%S %z] [%^%L%$] %v");

    if (print_debug_logs) {
        spdlog::set_level(spdlog::level::debug);
    } else {
        spdlog::set_level(spdlog::level::info);
    }

    // Register an atexit() callback so we do not need manual shutdown in app code
    std::atexit(AtExitWrapper);
}


} // namespace lora
