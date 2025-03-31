#include "mm_headers.h"

#define SDRAM_BASE (0x20000000)
#define SDRAM_SPAN (0x00020000)

// map_mem
void *map_mem(int fd, off_t base, size_t span)
{
    void *ret = mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
    if (ret == MAP_FAILED)
    {
        perror("Error mapping memory.");
        return NULL;
    }
    return ret;
}

int running; 
void sigint_handler(int sig) { running = 0; }

int main()
{
    signal(SIGINT, sigint_handler);
    
    // open mem dev
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0)
    {
        perror("Couldn't open /dev/mem\n");
        return -1;
    }

    void* sdram_map = map_mem(mem_fd, SDRAM_BASE, SDRAM_SPAN);
    if (!sdram_map)
    {
        perror("Error mapping memory.");
        return -1;
    }

    printf("Mapped address: %p\n", sdram_map);
    uint32_t* data = ((uint32_t*)sdram_map);

    // put 4*128/32 = 16 words
    for (int i = 0; i < 16; i++)
    {
        data[i] = (i+10) * (i+10);
        printf("addr=%p, data=%d\n", &data[i], data[i]);
    }

    printf("Data written.\n");
    getchar();

    data = ((uint32_t*)(sdram_map + 0x10000));
    for (int i = 0; i < 16; i++)
        printf("addr=%p, data=%d\n", &data[i], data[i]);
    
    munmap(SDRAM_BASE, SDRAM_SPAN);
    close(mem_fd);
    printf("Cleaned up.\n");
    
    return 0;
}