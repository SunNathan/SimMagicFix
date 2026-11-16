#pragma once

// Self-contained lock and update-waiter helpers compatible with the Le Mans
// Ultimate shared memory interface. The official SDK header is not required.

#include <windows.h>

#include <optional>
#include <utility>

namespace simagicfix::lmu {

class SharedMemoryLock {
public:
    static std::optional<SharedMemoryLock> MakeSharedMemoryLock() {
        SharedMemoryLock lock;
        if (lock.Init()) {
            return std::move(lock);
        }
        return std::nullopt;
    }

    bool Lock(DWORD timeout = INFINITE) {
        if (data_ == nullptr) {
            return false;
        }
        for (int spins = 0; spins < 4000; ++spins) {
            if (InterlockedCompareExchange(&data_->busy, 1, 0) == 0) {
                return true;
            }
            YieldProcessor();
        }
        InterlockedIncrement(&data_->waiters);
        bool acquired = false;
        while (true) {
            if (InterlockedCompareExchange(&data_->busy, 1, 0) == 0) {
                acquired = true;
                break;
            }
            if (event_ == nullptr ||
                WaitForSingleObject(event_, timeout) != WAIT_OBJECT_0) {
                break;
            }
        }
        InterlockedDecrement(&data_->waiters);
        return acquired;
    }

    void Unlock() {
        if (data_ == nullptr) {
            return;
        }
        InterlockedExchange(&data_->busy, 0);
        if (data_->waiters > 0 && event_ != nullptr) {
            SetEvent(event_);
        }
    }

    ~SharedMemoryLock() { Close(); }

    SharedMemoryLock(SharedMemoryLock&& other) noexcept
        : map_(std::exchange(other.map_, nullptr)),
          event_(std::exchange(other.event_, nullptr)),
          data_(std::exchange(other.data_, nullptr)) {}

    SharedMemoryLock& operator=(SharedMemoryLock&& other) noexcept {
        if (this != &other) {
            Close();
            map_ = std::exchange(other.map_, nullptr);
            event_ = std::exchange(other.event_, nullptr);
            data_ = std::exchange(other.data_, nullptr);
        }
        return *this;
    }

    SharedMemoryLock(const SharedMemoryLock&) = delete;
    SharedMemoryLock& operator=(const SharedMemoryLock&) = delete;

private:
    struct LockData {
        volatile LONG waiters;
        volatile LONG busy;
    };

    SharedMemoryLock() = default;

    bool Init() {
        map_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  sizeof(LockData), "LMU_SharedMemoryLockData");
        if (map_ == nullptr) {
            return false;
        }
        data_ = static_cast<LockData*>(
            MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LockData)));
        if (data_ == nullptr) {
            Close();
            return false;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            data_->waiters = 0;
            data_->busy = 0;
        }
        event_ = CreateEventA(nullptr, FALSE, FALSE, "LMU_SharedMemoryLockEvent");
        if (event_ == nullptr) {
            Close();
            return false;
        }
        return true;
    }

    void Close() {
        if (event_ != nullptr) {
            CloseHandle(event_);
            event_ = nullptr;
        }
        if (data_ != nullptr) {
            UnmapViewOfFile(data_);
            data_ = nullptr;
        }
        if (map_ != nullptr) {
            CloseHandle(map_);
            map_ = nullptr;
        }
    }

    HANDLE map_ = nullptr;
    HANDLE event_ = nullptr;
    LockData* data_ = nullptr;
};

class SharedMemoryUpdateWaiter {
public:
    static std::optional<SharedMemoryUpdateWaiter> MakeSharedMemoryUpdateWaiter() {
        SharedMemoryUpdateWaiter waiter;
        if (waiter.Init()) {
            return std::move(waiter);
        }
        return std::nullopt;
    }

    bool WaitForNextFrame(DWORD timeout = INFINITE) {
        if (hold_ == nullptr || data_ == nullptr) {
            return false;
        }
        if (WaitForSingleObject(hold_, timeout) != WAIT_OBJECT_0) {
            return false;
        }
        return WaitForSingleObject(data_, timeout) == WAIT_OBJECT_0;
    }

    ~SharedMemoryUpdateWaiter() { Close(); }

    SharedMemoryUpdateWaiter(SharedMemoryUpdateWaiter&& other) noexcept
        : hold_(std::exchange(other.hold_, nullptr)),
          data_(std::exchange(other.data_, nullptr)) {}

    SharedMemoryUpdateWaiter& operator=(SharedMemoryUpdateWaiter&& other) noexcept {
        if (this != &other) {
            Close();
            hold_ = std::exchange(other.hold_, nullptr);
            data_ = std::exchange(other.data_, nullptr);
        }
        return *this;
    }

    SharedMemoryUpdateWaiter(const SharedMemoryUpdateWaiter&) = delete;
    SharedMemoryUpdateWaiter& operator=(const SharedMemoryUpdateWaiter&) = delete;

private:
    SharedMemoryUpdateWaiter() = default;

    bool Init() {
        hold_ = OpenEventA(SYNCHRONIZE, FALSE, "LMU_Data_HoldEvent");
        if (hold_ == nullptr) {
            return false;
        }
        data_ = OpenEventA(SYNCHRONIZE, FALSE, "LMU_Data_DataEvent");
        if (data_ == nullptr) {
            Close();
            return false;
        }
        return true;
    }

    void Close() {
        if (hold_ != nullptr) {
            CloseHandle(hold_);
            hold_ = nullptr;
        }
        if (data_ != nullptr) {
            CloseHandle(data_);
            data_ = nullptr;
        }
    }

    HANDLE hold_ = nullptr;
    HANDLE data_ = nullptr;
};

}  // namespace simagicfix::lmu
