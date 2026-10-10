#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <ps5/kernel.h>

int main(void)
{
    FILE *out = fopen("/data/homebrew/PPSA99169/tests/privilege-inventory.txt", "w");
    if (!out) return 1;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0)) return 2;
    char *buf = malloc(size);
    if (!buf || sysctl(mib, 4, buf, &size, NULL, 0)) return 3;
    fprintf(out, "observer_pid=%d\n", getpid());
    for (char *p = buf; p + sizeof(struct kinfo_proc) <= buf + size;) {
        struct kinfo_proc *k = (struct kinfo_proc *)p;
        if (k->ki_structsize <= 0 || (size_t)k->ki_structsize < sizeof(*k)) break;
        p += k->ki_structsize;
        fprintf(out, "pid=%d comm=%s uid=%u ruid=%u svuid=%u rgid=%u jail_id=%d threads=%d",
                k->ki_pid, k->ki_comm, k->ki_uid, k->ki_ruid, k->ki_svuid,
                k->ki_rgid, k->ki_jid, k->ki_numthreads);
        if (k->ki_pid > 1) {
            unsigned char caps[16] = {0};
            int rc = kernel_get_ucred_caps(k->ki_pid, caps);
            fprintf(out, " authid=%016llx caps_rc=%d caps=", (unsigned long long)kernel_get_ucred_authid(k->ki_pid), rc);
            for (int i = 0; i < 16; ++i) fprintf(out, "%02x", caps[i]);
            fprintf(out, " root_system=%d jail_null=%d", kernel_get_proc_rootdir(k->ki_pid) == kernel_get_root_vnode(), !kernel_get_proc_jaildir(k->ki_pid));
        }
        fputc('\n', out);
    }
    free(buf);
    fclose(out);
    return 0;
}
