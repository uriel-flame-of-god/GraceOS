// ============================
// spm - Userland interface to the Security Policy Manager
// ============================

#include "../../lib/libgrace/grace.h"
#include "../../lib/libc/string.h"
#include "../../include/grace/spm_syscalls.h"

#define PASS_BUF_MAX 64

static void print_usage(void)
{
    print("Usage:\n");
    print("  spm user add <username> --uid <uid>\n");
    print("  spm user list\n");
    print("  spm user passwd <username>\n");
    print("  spm perm grant <user> <perm> <target>\n");
    print("  spm perm list <user>\n");
    print("  spm can [--as <user>] <perm> <target>\n");
    print("  spm whoami\n");
}

static void print_u32(uint32_t value)
{
    char buf[16];
    int i = 0;

    if (value == 0)
    {
        print("0");
        return;
    }

    while (value > 0 && i < (int)(sizeof(buf) - 1))
    {
        buf[i++] = '0' + (char)(value % 10);
        value /= 10;
    }

    while (i > 0)
    {
        char c[2] = { buf[--i], '\0' };
        print(c);
    }
}

static int parse_u32(const char* s, uint32_t* out)
{
    if (!s || !*s || !out)
        return -1;

    uint32_t value = 0;
    for (int i = 0; s[i]; i++)
    {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        value = (value * 10) + (uint32_t)(s[i] - '0');
    }

    *out = value;
    return 0;
}

static int lookup_user_uid(const char* name, uint32_t* out_uid)
{
    if (!name || !out_uid)
        return -1;

    if (parse_u32(name, out_uid) == 0)
        return 0;

    spm_user_info_t user;
    for (int i = 0; spm_user_enum(i, &user) == 0; i++)
    {
        if (user.active && strcmp(user.name, name) == 0)
        {
            *out_uid = user.uid;
            return 0;
        }
    }

    return -1;
}

static int lookup_user_name(uint32_t uid, char* out, size_t out_len)
{
    if (!out || out_len == 0)
        return -1;

    spm_user_info_t user;
    for (int i = 0; spm_user_enum(i, &user) == 0; i++)
    {
        if (user.active && user.uid == uid)
        {
            strncpy(out, user.name, out_len - 1);
            out[out_len - 1] = '\0';
            return 0;
        }
    }

    return -1;
}

static int parse_perm(const char* s, uint32_t* out_perm)
{
    if (!s || !out_perm)
        return -1;

    if (strcmp(s, "read") == 0) { *out_perm = PERM_READ; return 0; }
    if (strcmp(s, "write") == 0) { *out_perm = PERM_WRITE; return 0; }
    if (strcmp(s, "exec") == 0) { *out_perm = PERM_EXEC; return 0; }
    if (strcmp(s, "spawn") == 0) { *out_perm = PERM_SPAWN; return 0; }
    if (strcmp(s, "kill") == 0) { *out_perm = PERM_KILL; return 0; }
    if (strcmp(s, "pause") == 0) { *out_perm = PERM_PAUSE; return 0; }
    if (strcmp(s, "grant") == 0) { *out_perm = PERM_GRANT; return 0; }
    if (strcmp(s, "admin") == 0) { *out_perm = PERM_ADMIN; return 0; }
    if (strcmp(s, "all") == 0) { *out_perm = PERM_ALL; return 0; }

    return -1;
}

static void format_perm(uint32_t perm, char* out, size_t out_len)
{
    if (!out || out_len == 0)
        return;

    out[0] = '\0';

    if (perm & PERM_READ)  strcat(out, "read|");
    if (perm & PERM_WRITE) strcat(out, "write|");
    if (perm & PERM_EXEC)  strcat(out, "exec|");
    if (perm & PERM_SPAWN) strcat(out, "spawn|");
    if (perm & PERM_KILL)  strcat(out, "kill|");
    if (perm & PERM_PAUSE) strcat(out, "pause|");
    if (perm & PERM_GRANT) strcat(out, "grant|");
    if (perm & PERM_ADMIN) strcat(out, "admin|");

    size_t len = strlen(out);
    if (len == 0)
    {
        strncpy(out, "none", out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }

    if (out[len - 1] == '|')
        out[len - 1] = '\0';
}

static void read_password(const char* prompt, char* out, size_t out_len)
{
    if (!out || out_len == 0)
        return;

    out[0] = '\0';
    print(prompt);

    size_t len = 0;
    while (1)
    {
        int key = getkey();
        if (key == '\n' || key == '\r')
            break;
        if (key == 0x08 || key == 0x7F)
        {
            if (len > 0)
                len--;
            continue;
        }

        if (len + 1 < out_len)
            out[len++] = (char)key;
    }

    out[len] = '\0';
    print("\n");
}

static int cmd_user_add(int argc, char** argv)
{
    if (argc < 6 || strcmp(argv[4], "--uid") != 0)
    {
        print("spm: invalid user add syntax\n");
        return -1;
    }

    uint32_t uid = 0;
    if (parse_u32(argv[5], &uid) != 0)
    {
        print("spm: invalid uid\n");
        return -1;
    }

    if (spm_user_add(uid, argv[3]) != 0)
    {
        print("spm: user add failed\n");
        return -1;
    }

    print("spm: user added\n");
    return 0;
}

static int cmd_user_list(void)
{
    spm_user_info_t user;
    int found = 0;

    for (int i = 0; spm_user_enum(i, &user) == 0; i++)
    {
        if (!user.active)
            continue;

        print("UID ");
        print_u32(user.uid);
        print(" ");
        println(user.name);
        found = 1;
    }

    if (!found)
        print("spm: no users\n");

    return 0;
}

static int cmd_user_passwd(int argc, char** argv)
{
    if (argc < 4)
    {
        print("spm: missing username\n");
        return -1;
    }

    uint32_t uid = 0;
    if (lookup_user_uid(argv[3], &uid) != 0)
    {
        print("spm: user not found\n");
        return -1;
    }

    char pass1[PASS_BUF_MAX];
    char pass2[PASS_BUF_MAX];

    read_password("New password: ", pass1, sizeof(pass1));
    read_password("Confirm password: ", pass2, sizeof(pass2));

    if (strcmp(pass1, pass2) != 0)
    {
        print("spm: passwords do not match\n");
        return -1;
    }

    if (spm_user_passwd(uid, pass1) != 0)
    {
        print("spm: password update failed\n");
        return -1;
    }

    print("spm: password updated\n");
    return 0;
}

static int cmd_perm_grant(int argc, char** argv)
{
    if (argc < 6)
    {
        print("spm: invalid perm grant syntax\n");
        return -1;
    }

    uint32_t uid = 0;
    if (lookup_user_uid(argv[3], &uid) != 0)
    {
        print("spm: user not found\n");
        return -1;
    }

    uint32_t perm = 0;
    if (parse_perm(argv[4], &perm) != 0)
    {
        print("spm: invalid perm\n");
        return -1;
    }

    if (spm_cap_grant(uid, perm, argv[5]) != 0)
    {
        print("spm: perm grant failed\n");
        return -1;
    }

    print("spm: perm granted\n");
    return 0;
}

static int cmd_perm_list(int argc, char** argv)
{
    if (argc < 4)
    {
        print("spm: missing user\n");
        return -1;
    }

    uint32_t uid = 0;
    if (lookup_user_uid(argv[3], &uid) != 0)
    {
        print("spm: user not found\n");
        return -1;
    }

    spm_cap_info_t cap;
    int found = 0;

    for (int i = 0; spm_cap_enum(i, &cap) == 0; i++)
    {
        if (!cap.active || cap.uid != uid)
            continue;

        char perm_buf[64];
        format_perm(cap.perm, perm_buf, sizeof(perm_buf));

        print(perm_buf);
        print(" ");
        println(cap.target);
        found = 1;
    }

    if (!found)
        print("spm: no caps\n");

    return 0;
}

static int cmd_can(int argc, char** argv)
{
    int argi = 2;
    uint32_t uid = (uint32_t)getuid();

    if (argc > 4 && strcmp(argv[argi], "--as") == 0)
    {
        if (argc < 6)
        {
            print("spm: invalid --as usage\n");
            return -1;
        }

        if (lookup_user_uid(argv[argi + 1], &uid) != 0)
        {
            print("spm: user not found\n");
            return -1;
        }
        argi += 2;
    }

    if (argc <= argi + 1)
    {
        print("spm: invalid can syntax\n");
        return -1;
    }

    uint32_t perm = 0;
    if (parse_perm(argv[argi], &perm) != 0)
    {
        print("spm: invalid perm\n");
        return -1;
    }

    if (spm_check_user(uid, perm, argv[argi + 1]) == 0)
        print("true\n");
    else
        print("false\n");

    return 0;
}

static int cmd_whoami(void)
{
    uint32_t uid = (uint32_t)getuid();
    char name[SPM_NAME_MAX];

    if (lookup_user_name(uid, name, sizeof(name)) == 0)
    {
        print(name);
        print(" (uid=");
        print_u32(uid);
        print(")\n");
        return 0;
    }

    print("uid=");
    print_u32(uid);
    print("\n");
    return 0;
}

int spm_main(int argc, char** argv)
{
    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "user") == 0)
    {
        if (argc < 3)
        {
            print_usage();
            return 1;
        }

        if (strcmp(argv[2], "add") == 0)
            return cmd_user_add(argc, argv) == 0 ? 0 : 1;
        if (strcmp(argv[2], "list") == 0)
            return cmd_user_list() == 0 ? 0 : 1;
        if (strcmp(argv[2], "passwd") == 0)
            return cmd_user_passwd(argc, argv) == 0 ? 0 : 1;

        print("spm: unknown user command\n");
        return 1;
    }

    if (strcmp(argv[1], "perm") == 0)
    {
        if (argc < 3)
        {
            print_usage();
            return 1;
        }

        if (strcmp(argv[2], "grant") == 0)
            return cmd_perm_grant(argc, argv) == 0 ? 0 : 1;
        if (strcmp(argv[2], "list") == 0)
            return cmd_perm_list(argc, argv) == 0 ? 0 : 1;

        print("spm: unknown perm command\n");
        return 1;
    }

    if (strcmp(argv[1], "can") == 0)
        return cmd_can(argc, argv) == 0 ? 0 : 1;

    if (strcmp(argv[1], "whoami") == 0)
        return cmd_whoami() == 0 ? 0 : 1;

    print_usage();
    return 1;
}

#ifdef GRACE_USERLAND_APP
int main(int argc, char** argv)
{
    return spm_main(argc, argv);
}
#endif
