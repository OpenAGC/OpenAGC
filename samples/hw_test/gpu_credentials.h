/*
 * gpu_credentials.h — PS5 GPU process credential bypass
 *
 * The kernel GPU ioctl handlers check cr_sceAuthId from the thread's ucred.
 * Homebrew payloads have an authid that fails these checks. This module
 * sets cr_sceAuthId to 0x4801000000000000, which passes the masked check
 * (0xd8e70400) while failing the exact-match check (0xd8e70ac0), allowing
 * GPU ioctls to proceed.
 *
 * Extracted from agc_init.c for reuse across hw_test samples.
 *
 * Requires: ps5-payload-sdk kernel functions (kernel_get_proc,
 *           kernel_copyout, kernel_copyin, kernel_get_ucred_authid,
 *           kernel_set_ucred_authid)
 */

#ifndef GPU_CREDENTIALS_H
#define GPU_CREDENTIALS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define GPU_AUTHID_REQUIRED  0x4801000000000000ULL
#define GPU_AUTHID_MASK      0xff0f000000000000ULL

/* Kernel struct offsets (FW 5.50):
 *   proc + 0x40  = p_ucred (pointer to ucred)
 *   thread + 0x140 = td_ucred (pointer to ucred)
 *   ucred + 0x58 = cr_sceAuthId (uint64)
 *   proc + 0x10  = p_threads (list_head, first thread)
 *   thread + 0x10 = td_plist.le_next (next thread)
 *   thread + 0x9c  = td_tid
 */
#define KERNEL_OFFSET_PROC_P_UCRED    0x40
#define KERNEL_OFFSET_UCRED_AUTHID    0x58
#define KERNEL_OFFSET_THREAD_UCRED    0x140

/*
 * set_gpu_credentials — patch cr_sceAuthId on p_ucred and all td_ucred
 *                       objects to 0x4801000000000000.
 *
 * Returns 0 on success, -1 on failure.
 */
static inline int set_gpu_credentials(void) {
    pid_t pid = getpid();
    uint64_t authid = 0;

    /* Read current authid from p_ucred */
    authid = kernel_get_ucred_authid(pid);
    printf("    GPU cred: current authid = 0x%016llx\n",
           (unsigned long long)authid);

    if ((authid & GPU_AUTHID_MASK) == GPU_AUTHID_REQUIRED) {
        printf("    GPU cred: already set, checking td_ucred\n");
    }

    /* Set authid on p_ucred */
    if (kernel_set_ucred_authid(pid, GPU_AUTHID_REQUIRED)) {
        printf("    GPU cred: kernel_set_ucred_authid failed\n");
        return -1;
    }

    /* Verify p_ucred was updated */
    authid = kernel_get_ucred_authid(pid);
    printf("    GPU cred: p_ucred authid = 0x%016llx (%s)\n",
           (unsigned long long)authid,
           (authid & GPU_AUTHID_MASK) == GPU_AUTHID_REQUIRED ? "OK" : "FAIL");

    /* Also patch td_ucred directly for each thread.
     * The kernel ioctl handler reads cr_sceAuthId from td_ucred
     * (curthread->td_ucred at thread+0x140), NOT from p_ucred.
     * kernel_set_ucred_authid modifies p_ucred. If td_ucred and
     * p_ucred point to the same ucred object (the normal case),
     * the modification is already visible. But if the exploit
     * changed p_ucred to point to a new ucred, td_ucred may still
     * point to the old one. */
    intptr_t proc = kernel_get_proc(pid);
    if (proc) {
        uint64_t p_ucred = 0;
        kernel_copyout(proc + KERNEL_OFFSET_PROC_P_UCRED, &p_ucred, sizeof(p_ucred));
        printf("    GPU cred: p_ucred = 0x%lx\n", (unsigned long)p_ucred);

        uint64_t thread_ptr = kernel_getlong(proc + 0x10);

        int patched = 0;
        int threads = 0;
        for (int i = 0; i < 16 && thread_ptr != 0; i++) {
            threads++;
            uint64_t td_ucred = 0;
            kernel_copyout(thread_ptr + KERNEL_OFFSET_THREAD_UCRED,
                          &td_ucred, sizeof(td_ucred));

            uint64_t tid = kernel_getlong(thread_ptr + 0x9c);
            printf("    GPU cred: thread[%d] tid=%d td_ucred=0x%lx",
                   i, (int)tid, (unsigned long)td_ucred);

            if (td_ucred != 0 && td_ucred != p_ucred) {
                uint64_t cur = 0;
                kernel_copyout(td_ucred + KERNEL_OFFSET_UCRED_AUTHID,
                              &cur, sizeof(cur));
                printf(" authid=0x%016llx (DIFFERS from p_ucred)",
                       (unsigned long long)cur);
                if ((cur & GPU_AUTHID_MASK) != GPU_AUTHID_REQUIRED) {
                    uint64_t new_id = GPU_AUTHID_REQUIRED;
                    kernel_copyin(&new_id,
                                 td_ucred + KERNEL_OFFSET_UCRED_AUTHID,
                                 sizeof(new_id));
                    printf(" → PATCHED");
                    patched++;
                }
            } else if (td_ucred == p_ucred) {
                printf(" (matches p_ucred)");
            } else {
                printf(" (NULL!)");
            }
            printf("\n");

            thread_ptr = kernel_getlong(thread_ptr + 0x10);
        }

        printf("    GPU cred: found %d thread(s), patched %d\n",
               threads, patched);
    } else {
        printf("    GPU cred: WARNING — kernel_get_proc failed\n");
    }

    /* Final verify */
    authid = kernel_get_ucred_authid(pid);
    return (authid & GPU_AUTHID_MASK) == GPU_AUTHID_REQUIRED ? 0 : -1;
}

#endif /* GPU_CREDENTIALS_H */
