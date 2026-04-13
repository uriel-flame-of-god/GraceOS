// ============================
// doas - SPM-gated privilege escalation wrapper
// ============================

#include "../../lib/libgrace/grace.h"
#include "../../lib/libc/string.h"
#include "../../include/grace/spm_syscalls.h"
#include "../../drivers/storage/bfs.h"

#define DOAS_CONFIG_PATH "/etc/doas.conf"
#define DOAS_MAX_RULES   64
#define DOAS_LINE_MAX    256
#define DOAS_FILE_MAX    4096
#define PASS_BUF_MAX     64

struct doas_rule {
    char user[32];
    char target[32];
    char command[64];
    int nopass;
    int permit;
};

extern struct bfs_instance g_bfs;

static void print_usage(void)
{
    print("Usage: doas <command> [args...]\n");
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

    if (strcmp(name, "root") == 0)
    {
        *out_uid = 0;
        return 0;
    }

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

    if (uid == 0)
    {
        strncpy(out, "root", out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }

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

static const char* trim_line(char* line)
{
    if (!line)
        return NULL;

    for (int i = 0; line[i]; i++)
    {
        if (line[i] == '\r' || line[i] == '\n')
        {
            line[i] = '\0';
            break;
        }
    }

    for (int i = 0; line[i]; i++)
    {
        if (line[i] == '#')
        {
            line[i] = '\0';
            break;
        }
    }

    return line;
}

static int next_token(char** cursor, char* out, size_t out_len)
{
    if (!cursor || !*cursor || !out || out_len == 0)
        return 0;

    char* p = *cursor;
    while (*p == ' ' || *p == '\t')
        p++;

    if (*p == '\0')
    {
        *cursor = p;
        return 0;
    }

    size_t i = 0;
    while (*p && *p != ' ' && *p != '\t')
    {
        if (i + 1 < out_len)
            out[i++] = *p;
        p++;
    }

    out[i] = '\0';
    *cursor = p;
    return i > 0;
}

static int parse_rules(struct doas_rule* rules, int max_rules)
{
    char buffer[DOAS_FILE_MAX];
    uint64_t out_size = 0;

    if (bfs_read_file(&g_bfs, DOAS_CONFIG_PATH, buffer, sizeof(buffer) - 1, &out_size) != 0)
        return -1;

    buffer[out_size] = '\0';

    int rule_count = 0;
    const char* p = buffer;

    while (*p && rule_count < max_rules)
    {
        char line[DOAS_LINE_MAX];
        int li = 0;

        while (*p && *p != '\n' && li < (DOAS_LINE_MAX - 1))
            line[li++] = *p++;
        if (*p == '\n')
            p++;
        line[li] = '\0';

        trim_line(line);

        char* cursor = line;
        char token[64];
        if (!next_token(&cursor, token, sizeof(token)))
            continue;

        struct doas_rule rule;
        memset(&rule, 0, sizeof(rule));

        if (strcmp(token, "permit") == 0)
            rule.permit = 1;
        else if (strcmp(token, "deny") == 0)
            rule.permit = 0;
        else
            continue;

        if (!next_token(&cursor, rule.user, sizeof(rule.user)))
            continue;

        strncpy(rule.target, "root", sizeof(rule.target) - 1);
        rule.target[sizeof(rule.target) - 1] = '\0';

        while (next_token(&cursor, token, sizeof(token)))
        {
            if (strcmp(token, "as") == 0)
            {
                if (!next_token(&cursor, rule.target, sizeof(rule.target)))
                    break;
            }
            else if (strcmp(token, "cmd") == 0)
            {
                if (!next_token(&cursor, rule.command, sizeof(rule.command)))
                    break;
            }
            else if (strcmp(token, "nopass") == 0)
            {
                rule.nopass = 1;
            }
        }

        rules[rule_count++] = rule;
    }

    return rule_count;
}

static int match_rule(const struct doas_rule* rule, const char* user, const char* cmd)
{
    if (!rule || !user || !cmd)
        return 0;

    if (strcmp(rule->user, user) != 0)
        return 0;

    if (rule->command[0] != '\0' && strcmp(rule->command, cmd) != 0)
        return 0;

    return 1;
}

static int find_matching_rule(const struct doas_rule* rules, int count, const char* user, const char* cmd, struct doas_rule* out)
{
    for (int i = 0; i < count; i++)
    {
        if (!match_rule(&rules[i], user, cmd))
            continue;

        if (!rules[i].permit)
            return 0;

        if (out)
            *out = rules[i];
        return 1;
    }

    return 0;
}

int doas_main(int argc, char** argv)
{
    if (argc < 2)
    {
        print_usage();
        return 1;
    }

    uint32_t uid = (uint32_t)getuid();
    char user[SPM_NAME_MAX];

    if (lookup_user_name(uid, user, sizeof(user)) != 0)
    {
        print("doas: unknown user\n");
        return 1;
    }

    struct doas_rule rules[DOAS_MAX_RULES];
    int rule_count = parse_rules(rules, DOAS_MAX_RULES);
    if (rule_count <= 0)
    {
        print("doas: permission denied\n");
        return 1;
    }

    struct doas_rule rule;
    int allowed = find_matching_rule(rules, rule_count, user, argv[1], &rule);
    if (!allowed)
    {
        print("doas: permission denied\n");
        return 1;
    }

    if (!rule.nopass)
    {
        char pass[PASS_BUF_MAX];
        read_password("Password: ", pass, sizeof(pass));

        if (spm_user_auth(uid, pass) != 0)
        {
            print("doas: authentication failed\n");
            return 1;
        }
    }

    uint32_t target_uid = 0;
    if (lookup_user_uid(rule.target, &target_uid) != 0)
    {
        print("doas: invalid target user\n");
        return 1;
    }

    if (setuid((uid_t)target_uid) < 0)
    {
        print("doas: setuid failed\n");
        return 1;
    }

    if (exec(argv[1]) < 0)
    {
        print("doas: exec failed\n");
        return 1;
    }

    return 1;
}

#ifdef GRACE_USERLAND_APP
int main(int argc, char** argv)
{
    return doas_main(argc, argv);
}
#endif
