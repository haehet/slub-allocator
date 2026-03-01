#include "utils.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <stdexcept>

namespace util {

uint64_t get_urandom() {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        throw std::runtime_error("Failed to open /dev/urandom");
    }
    uint64_t value;
    if (read(fd, &value, sizeof(value)) != sizeof(value)) {
        close(fd);
        throw std::runtime_error("Failed to read from /dev/urandom");
    }
    close(fd);
    return value;
}


} 