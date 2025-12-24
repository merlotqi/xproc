#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xproc/platform/platform.hpp>
#include <xproc/shm/shm.hpp>

namespace xproc {
namespace shm {

shm::shm(shm &&other) noexcept
{
    *this = std::move(other);
}

shm &shm::operator=(shm &&other) noexcept
{
    if (this != &other)
    {
        detach();

        addr_ = other.addr_;
        size_ = other.size_;
        fd_ = other.fd_;
        name_ = std::move(other.name_);

        other.addr_ = nullptr;
        other.size_ = 0;
        other.fd_ = -1;
    }
    return *this;
}

bool shm::open(const std::string &name, size_t size, shm_open_mode mode)
{
    name_ = name;
    size_ = size;

    int oflag = 0;
    if (mode == shm_open_mode::create)
        oflag = O_CREAT | O_RDWR;
    else if (mode == shm_open_mode::open)
        oflag = O_RDWR;
    else if (mode == shm_open_mode::open_create)
        oflag = O_CREAT | O_RDWR;
    else if (mode == shm_open_mode::read)
        oflag = O_RDONLY;

    fd_ = shm_open(name_.c_str(), oflag, 0666);
    if (fd_ == -1)
        return false;

    if (mode != shm_open_mode::read)
    {
        if (ftruncate(fd_, size_) == -1)
        {
            ::close(fd_);
            return false;
        }
    }

    int port = PROT_READ | (mode == shm_open_mode::read ? 0 : PROT_WRITE);
    addr_ = mmap(nullptr, size_, port, MAP_SHARED, fd_, 0);

    if (addr_ == MAP_FAILED)
    {
        addr_ = nullptr;
        ::close(fd_);
        return false;
    }
    return true;
}

void shm::detach()
{
    if (addr_)
    {
        munmap(addr_, size_);
        addr_ = nullptr;
    }

    if (fd_ != -1)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

void shm::unlink(const std::string &name)
{
    shm_unlink(name.c_str());
}

}// namespace shm
}// namespace xproc
