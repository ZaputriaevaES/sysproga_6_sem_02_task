#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define PAGEMAP_LENGTH 8
#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define VPN_LEVELS 5  
#define VPN_MASK 0x1FF

// Page table levels for Sv57
// PGD→P4D→PUD→PMD→PTE for Sv57
const int VPN_SHIFTS[VPN_LEVELS] = {12, 21, 30, 39, 48};

// Stores information about pagemap entry (PFN, flags).
// Example of a pagemap entry:
// 0x7fff75d20000 : pfn 0 soft-dirty 1 file/shared 0 swapped 0 present 1
// Pegemap entry structure: [pfn soft_dirty file_page swapped present]
// Bits  | Contents
// ------|------------------------
// 0-53  | PFN (Page Frame Number)
// 54    | Soft-dirty flag
// 55-60 | Reserved (must be 0)
// 61    | File-page flag
// 62    | Swapped flag
// 63    | Present flag
struct pagemap_entry 
{
    uint64_t pfn : 54;
    unsigned int soft_dirty : 1;
    unsigned int file_page : 1;
    unsigned int swapped : 1;
    unsigned int present : 1;
};

// Translation tree node (array of child nodes + PFN for PTE).
typedef struct PageTableNode 
{
    struct PageTableNode *children[512]; // 2^9 possible VPNs at each level
    uint64_t pfn;                        // For leaves only (PTE)
} PageTableNode;

PageTableNode* create_node() 
{
    PageTableNode *node = calloc(1, sizeof(PageTableNode));
    return node;
}

void free_tree(PageTableNode *root) 
{
    if (!root) return;
    for (int i = 0; i < 512; i++) 
    {
        if (root->children[i]) 
        {
            free_tree(root->children[i]);
        }
    }
    free(root);
}

// Split virtual address into VPN (Virtual Page Number) for each layer.
void get_vpn_levels(uintptr_t addr, uint64_t *vpns) 
{
    for (int i = 0; i < VPN_LEVELS; i++) 
    {
        vpns[VPN_LEVELS - 1 - i] = (addr >> VPN_SHIFTS[i]) & VPN_MASK;
    }
}

// Read entry from /proc/[pid]/pagemap.
int pagemap_get_entry(struct pagemap_entry *entry, int pagemap_file, uintptr_t vaddr) 
{
    size_t nread = 0;
    ssize_t ret;
    uint64_t data;
    
    // pread reads data from the pagemap file at the position corresponding to the virtual address
    // pread reads 8 bytes (PAGEMAP_LENGTH) from the pagemap file
    // The position is calculated as (vaddr / page_size) * 8 (since each entry takes 8 bytes)
    // The reading is done in parts in case the system call does not return all the data at once

    while (nread < PAGEMAP_LENGTH) 
    {
        ret = pread(pagemap_file, ((uint8_t*)&data) + nread, PAGEMAP_LENGTH - nread, (vaddr / PAGE_SIZE) * PAGEMAP_LENGTH + nread);
        if (ret <= 0) 
        {
            printf("Pagemap_get_entry error\n");
            return 1;
        }
        nread += ret;
    }

    entry->pfn = data & (((uint64_t)1 << 54) - 1);
    entry->soft_dirty = (data >> 54) & 1;
    entry->file_page = (data >> 61) & 1;
    entry->swapped = (data >> 62) & 1;
    entry->present = (data >> 63) & 1;
    return 0;
}

// Build translation tree
void build_translation_tree(PageTableNode *root, int pagemap_file, uintptr_t start, uintptr_t end) 
{
    // For each virtual address in the range [start, end]:
    for (uintptr_t addr = start; addr < end; addr += PAGE_SIZE) 
    {
        struct pagemap_entry entry;
        // Gets an entry from pagemap.
        if (pagemap_get_entry(&entry, pagemap_file, addr)) 
        {
            continue;
        }

        // If the page is present (entry.present == 1), parses the VPN.
        if (entry.present) 
        {
            uint64_t vpns[VPN_LEVELS];
            get_vpn_levels(addr, vpns);

            PageTableNode *current = root;
            // Builds a tree:
            // We go through ALL 5 levels (including PTE)
            // For each level (PGD → P4D → PUD → PMD → PTE) creates child nodes.
            for (int i = 0; i < VPN_LEVELS; i++) 
            {
                if (i == VPN_LEVELS - 1) 
                {
                    // For the last level (PTE) we save PFN
                    current->pfn = entry.pfn;
                } 
                else 
                {
                    // For the remaining levels we create child nodes
                    if (!current->children[vpns[i]]) 
                    {
                        current->children[vpns[i]] = create_node();
                    }
                    current = current->children[vpns[i]];
                }
            }
        }
    }
}

// Print tree in readable format
void print_tree(PageTableNode *node, int level, const char *prefix) 
{
    char new_prefix[256];
    
    for (int i = 0; i < 512; i++) 
    {
        if (node->children[i]) 
        {
            snprintf(new_prefix, sizeof(new_prefix), "%s ── 0x%x", prefix, i);
            
            if (level == VPN_LEVELS - 2) 
            {
                // If this is the penultimate level (PMD), the next one is PTE
                printf("%s ── PTE: PFN=0x%lx\n", new_prefix, node->children[i]->pfn);
            } 
            else 
            {
                printf("%s ─┐\n", new_prefix);
                print_tree(node->children[i], level + 1, new_prefix);
            }
        }
    }
}

// Open /proc/[pid]/maps and /proc/[pid]/pagemap.
// Build the translation tree.
// Print the result.
int main(int argc, char *argv[]) 
{
    if (argc != 2) 
    {
        printf("Incorrect [pid]\n");
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    printf("Building translation tree for PID: %d\n", pid);

    // Open /proc/[pid]/maps and /proc/[pid]/pagemap
    char maps_path[256], pagemap_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    snprintf(pagemap_path, sizeof(pagemap_path), "/proc/%d/pagemap", pid);

    int pagemap_file = open(pagemap_path, O_RDONLY);
    if (pagemap_file < 0) 
    {
        printf("Open pagemap error\n");
        return 1;
    }

    FILE *maps_file = fopen(maps_path, "r");
    if (!maps_file) 
    {
        printf("Open maps error\n");
        close(pagemap_file);
        return 1;
    }

    PageTableNode *root = create_node();
    char line[256];
    
    // Read /proc/[pid]/maps line by line. Each line describes one range of virtual memory
    // /proc/[pid]/maps entry format: [start-end permissions offset dev inode pathname]
    // line - /proc/[pid]/maps entry
    // start - /proc/[pid]/maps start
    // end - /proc/[pid]/maps start
    while (fgets(line, sizeof(line), maps_file)) 
    {
        uintptr_t start, end;
        if (sscanf(line, "%lx-%lx", &start, &end) == 2) 
        {
            build_translation_tree(root, pagemap_file, start, end);
        }
    }

    printf("Page table hierarchy:\n");
    print_tree(root, 0, "PGD");

    free_tree(root);
    fclose(maps_file);
    close(pagemap_file);
    return 0;
}

