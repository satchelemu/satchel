// Bounded metadata-only missing-command summaries. No guest memory is inspected.
#include <stdio.h>
#include <limits.h>
#include "MissingServiceNames.inc"
extern uint64_t dm_nvn_frame_count(void);
static struct {
    char service[16], interface[80];
    uint32_t command;
    const char *method;
    uint64_t first_frame, count;
} satchel_missing[1024];
static unsigned satchel_missing_count;
static pthread_mutex_t satchel_missing_mu = PTHREAD_MUTEX_INITIALIZER;
static const char *satchel_interface_name(const char *kind) {
    static const char *const aliases[][2] = {
        {"acc:profile", "IProfile"}, {"time:local", "ISystemClock"}, {"time:adminuser", "ISystemClock"}, {"time:adminlocal", "ISystemClock"}, {"time:systemnetwork", "ISystemClock"}, {"time:repairsteady", "ISteadyClock"},
        {"time:network", "ISystemClock"}, {"time:steady", "ISteadyClock"},
        {"nifm:general", "IGeneralService"}, {"nifm:request", "IRequest"},
        {"friend:service", "FriendService"}, {"friend:notify", "NotificationService"}, {"apm:session", "ISession"}
    };
    for (unsigned i = 0; i < sizeof aliases / sizeof *aliases; i++)
        if (!strcmp(kind, aliases[i][0])) return aliases[i][1];
    for (unsigned i = 0; i < sizeof satchel_command_names / sizeof *satchel_command_names; i++)
        if (!strcmp(kind, satchel_command_names[i].service)) return satchel_command_names[i].interface;
    return kind;
}
static void satchel_missing_flush(void) {
    const char *path = getenv("DM_MISSING_SERVICES");
    if (!path || !*path) return;
    char temp[PATH_MAX];
    if (snprintf(temp, sizeof temp, "%s.tmp", path) >= (int)sizeof temp) return;
    int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); return; }
    fputs("[\n", f);
    for (unsigned i = 0; i < satchel_missing_count; i++) {
        fprintf(f, "%s{\"service\":\"%s\",\"interface\":\"%s\",\"command\":%u,\"method\":",
                i ? ",\n" : "", satchel_missing[i].service, satchel_missing[i].interface, satchel_missing[i].command);
        if (satchel_missing[i].method) fprintf(f, "\"%s\"", satchel_missing[i].method); else fputs("null", f);
        fprintf(f, ",\"firstSeenFrame\":%llu,\"count\":%llu}",
                (unsigned long long)satchel_missing[i].first_frame, (unsigned long long)satchel_missing[i].count);
    }
    fputs("\n]\n", f);
    int failed = ferror(f);
    if (fclose(f)) failed = 1;
    if (!failed) (void)rename(temp, path);
}
static void satchel_missing_record(const char *service, const char *kind, uint32_t command) {
    char safe_service[16], interface[80];
    satchel_safe_name(safe_service, sizeof safe_service, service);
    const char *resolved = satchel_interface_name(kind ? kind : service);
    if (!kind && resolved == service) resolved = "unknown";
    satchel_safe_name(interface, sizeof interface, resolved);
    pthread_mutex_lock(&satchel_missing_mu);
    unsigned i = 0;
    for (; i < satchel_missing_count; i++)
        if (satchel_missing[i].command == command && !strcmp(satchel_missing[i].service, safe_service) &&
            !strcmp(satchel_missing[i].interface, interface)) break;
    if (i < 1024) {
        int added = i == satchel_missing_count;
        if (added) {
            satchel_missing_count++;
            strlcpy(satchel_missing[i].service, safe_service, sizeof satchel_missing[i].service);
            strlcpy(satchel_missing[i].interface, interface, sizeof satchel_missing[i].interface);
            satchel_missing[i].command = command; satchel_missing[i].first_frame = dm_nvn_frame_count();
            for (unsigned j = 0; j < sizeof satchel_command_names / sizeof *satchel_command_names; j++)
                if (command == satchel_command_names[j].command && !strcmp(interface, satchel_command_names[j].interface) &&
                    (!strcmp(safe_service, satchel_command_names[j].service) || !strcmp("(object)", satchel_command_names[j].service))) {
                    satchel_missing[i].method = satchel_command_names[j].method; break;
                }
        }
        if (satchel_missing[i].count != UINT64_MAX) satchel_missing[i].count++;
        // A game may retry a missing command every frame: rewrite the file for a new
        // command and then only at power-of-two counts, so the count stays close.
        uint64_t c = satchel_missing[i].count;
        if (added || !(c & (c - 1))) satchel_missing_flush();
    }
    pthread_mutex_unlock(&satchel_missing_mu);
}
static void satchel_missing_reset(void) {
    pthread_mutex_lock(&satchel_missing_mu);
    memset(satchel_missing, 0, sizeof satchel_missing); satchel_missing_count = 0;
    // Teardown resets memory but retains the completed title's diagnostics.
    // SessionBridge initializes the next launch's file beside its fresh host.log.
    pthread_mutex_unlock(&satchel_missing_mu);
}
