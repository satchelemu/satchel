#ifndef SATCHEL_SWITCH_PROCESS_H
#define SATCHEL_SWITCH_PROCESS_H
#include "CPUProbe.h"
#include "NSOLoader.h"
#include "ProcessMetadata.h"
#include "SwitchStorage.h"

typedef struct {
    const char *name;
    SatchelNSOInfo info;
    const uint8_t *image;
    size_t image_size;
} SatchelSwitchProcessModule;
typedef struct {
    uint64_t address, size;
    uint32_t state, attributes, permissions, ipc_refcount, device_refcount, padding;
} SatchelSwitchMemoryInfo;
typedef struct SatchelSwitchProcess SatchelSwitchProcess;
/* Immutable pre-run real-time sample; timezone is a name-only host profile,
 * not a supplied Nintendo conversion-rule database or a network sync claim. */
enum { SATCHEL_SWITCH_TIME_SEED_VERSION=1, SATCHEL_SWITCH_TIME_ZONE_HOST_NAME_ONLY=1,
       SATCHEL_SWITCH_TIME_SHARED_SIZE=4096 };
typedef struct {
    uint32_t version, size;
    int64_t unix_seconds;
    uint64_t system_tick;
    uint32_t unix_nanoseconds;
    char location_name[36];
} SatchelSwitchTimeSeed;
typedef struct {
    uint32_t configured, network_initialized, timezone_profile, shared_handles;
    uint64_t seed_attachments, clock_reads, timezone_reads, shared_handle_requests;
    uint64_t shared_map_requests, shared_unmap_requests, shared_maps_serviced, shared_unmaps_serviced;
    uint64_t shared_mappings, shared_backing_bytes;
} SatchelSwitchTimeInfo;
/* Valid version/size64; nanos<1e9; nonempty ASCII name, max35 bytes,
 * zero padding. Sample tick must be <=now and <=1s old. Once before run,
 * checked arithmetic and real4096-byte backing allocation precede commit.
 * Caller brackets actual wall-time sample with ticks <=100ms apart. */
int satchel_switch_process_attach_time_seed(SatchelSwitchProcess *, const SatchelSwitchTimeSeed *);
int satchel_switch_process_time_info(const SatchelSwitchProcess *, SatchelSwitchTimeInfo *);

enum { SATCHEL_SWITCH_THREAD_LIMIT = 32, SATCHEL_SWITCH_THREAD_WAIT_LIMIT = 64, SATCHEL_SWITCH_THREAD_QUANTUM = 1024 };
typedef enum { SATCHEL_SWITCH_THREAD_CREATED, SATCHEL_SWITCH_THREAD_RUNNABLE, SATCHEL_SWITCH_THREAD_WAITING,
    SATCHEL_SWITCH_THREAD_SLEEPING, SATCHEL_SWITCH_THREAD_EXITED, SATCHEL_SWITCH_THREAD_MUTEX_WAIT } SatchelSwitchThreadState;
typedef enum { SATCHEL_SWITCH_RUN_CPU_STOP, SATCHEL_SWITCH_RUN_BLOCKED, SATCHEL_SWITCH_RUN_EXITED } SatchelSwitchRunState;
typedef struct {
    SatchelSwitchRunState state;
    SatchelCPUProbeResult cpu;
    uint64_t thread_id, next_deadline_tick;
} SatchelSwitchRunResult;
typedef struct {
    uint64_t id, tls_address, affinity_mask, instructions;
    uint32_t handle, priority, cpu_id, state, handle_references, wait_references;
    int32_t ideal_core_id; // -1 means no preferred core; cpu_id is assigned core.
    uint32_t effective_priority; // priority above remains the base policy priority.
} SatchelSwitchThreadInfo;
typedef struct {
    uint64_t lock_requests, unlock_requests, waits, handoffs, current_waiters;
} SatchelSwitchMutexInfo;
/* Counts only; no user mutex contents, addresses or tags. Requests include
 * failures; waits count queued calls; handoffs count ownership transfers even
 * when the subsequent user-word write fails. Failure preserves out. */
int satchel_switch_process_mutex_info(const SatchelSwitchProcess *process, SatchelSwitchMutexInfo *out);
/* Serialized cooperative scheduler: one global instruction budget, bounded
 * priority quanta, no host worker thread or sleeping. BLOCKED has no runnable
 * context; next_deadline_tick=UINT64_MAX denotes no timed deadline. Borrowed
 * process_cpu names the CPU_STOP context; callers must fetch it again after
 * each such result and route traps to that context. It may be NULL after
 * BLOCKED/EXITED when the last active context was reclaimed; use the process
 * clock API for deadlines. No cached context survives a schedule call by contract. */
SatchelSwitchRunResult satchel_switch_process_run_scheduled(SatchelSwitchProcess *process, uint64_t budget,
    SatchelCPUProbeCancelled cancelled, void *context);
uint32_t satchel_switch_process_thread_count(const SatchelSwitchProcess *process);
/* Shared process clock through any retained context; failure preserves output. */
int satchel_switch_process_get_system_tick(SatchelSwitchProcess *process, uint64_t *ticks);
int satchel_switch_process_thread_info(const SatchelSwitchProcess *process, uint32_t index, SatchelSwitchThreadInfo *out);
typedef struct {
    uint32_t applet_message_events, gpu_error_events, suspension_events, threads, other_events;
} SatchelSwitchThreadWaitInfo;
/* Classifies retained WaitSynchronization entries only; no guest handles or
 * payloads. Non-waiting records return zeros; absent records preserve output. */
int satchel_switch_process_thread_wait_info(const SatchelSwitchProcess *process, uint32_t index, SatchelSwitchThreadWaitInfo *out);


enum { SATCHEL_SWITCH_IPC_SERVICE_NONE, SATCHEL_SWITCH_IPC_SERVICE_FSP, SATCHEL_SWITCH_IPC_SERVICE_NVDRV,
       SATCHEL_SWITCH_IPC_SERVICE_HID, SATCHEL_SWITCH_IPC_SERVICE_APPLET, SATCHEL_SWITCH_IPC_SERVICE_VI,
       SATCHEL_SWITCH_IPC_SERVICE_SET, SATCHEL_SWITCH_IPC_SERVICE_TIME, SATCHEL_SWITCH_IPC_SERVICE_OTHER,
       SATCHEL_SWITCH_IPC_SERVICE_CSRNG = 9, SATCHEL_SWITCH_IPC_SERVICE_PSC_M, SATCHEL_SWITCH_IPC_SERVICE_LM,
       SATCHEL_SWITCH_IPC_SERVICE_SPL, SATCHEL_SWITCH_IPC_SERVICE_FATAL, SATCHEL_SWITCH_IPC_SERVICE_LDR_RO,
       SATCHEL_SWITCH_IPC_SERVICE_RO_1, SATCHEL_SWITCH_IPC_SERVICE_APM, SATCHEL_SWITCH_IPC_SERVICE_APM_P,
       SATCHEL_SWITCH_IPC_SERVICE_ACC_U0, SATCHEL_SWITCH_IPC_SERVICE_NS_AM, SATCHEL_SWITCH_IPC_SERVICE_BPC,
       SATCHEL_SWITCH_IPC_SERVICE_AUDREN, SATCHEL_SWITCH_IPC_SERVICE_AUDOUT, SATCHEL_SWITCH_IPC_SERVICE_AUDIN,
       SATCHEL_SWITCH_IPC_SERVICE_NIFM, SATCHEL_SWITCH_IPC_SERVICE_BSD, SATCHEL_SWITCH_IPC_SERVICE_SSL,
       SATCHEL_SWITCH_IPC_SERVICE_PL, SATCHEL_SWITCH_IPC_SERVICE_PCTL, SATCHEL_SWITCH_IPC_SERVICE_FRIENDS,
       SATCHEL_SWITCH_IPC_SERVICE_SFDNSRES, SATCHEL_SWITCH_IPC_SERVICE_APM_AM, SATCHEL_SWITCH_IPC_SERVICE_APM_SYS,
       SATCHEL_SWITCH_IPC_SERVICE_FGM,
       SATCHEL_SWITCH_IPC_SERVICE_NVDRV_A, SATCHEL_SWITCH_IPC_SERVICE_APPLET_AE, SATCHEL_SWITCH_IPC_SERVICE_VI_M,
       SATCHEL_SWITCH_IPC_SERVICE_SET_SYS, SATCHEL_SWITCH_IPC_SERVICE_TIME_S, SATCHEL_SWITCH_IPC_SERVICE_SPL_MIG,
       SATCHEL_SWITCH_IPC_SERVICE_SPL_SSL, SATCHEL_SWITCH_IPC_SERVICE_SPL_FS, SATCHEL_SWITCH_IPC_SERVICE_SPL_ES,
       SATCHEL_SWITCH_IPC_SERVICE_SPL_MANU, SATCHEL_SWITCH_IPC_SERVICE_NS_AM2, SATCHEL_SWITCH_IPC_SERVICE_BPC_C,
       SATCHEL_SWITCH_IPC_SERVICE_NIFM_S, SATCHEL_SWITCH_IPC_SERVICE_NIFM_A, SATCHEL_SWITCH_IPC_SERVICE_BSD_S,
       SATCHEL_SWITCH_IPC_SERVICE_SSL_S, SATCHEL_SWITCH_IPC_SERVICE_PL_S, SATCHEL_SWITCH_IPC_SERVICE_PCTL_A,
       SATCHEL_SWITCH_IPC_SERVICE_PCTL_S, SATCHEL_SWITCH_IPC_SERVICE_PCTL_R, SATCHEL_SWITCH_IPC_SERVICE_FRIENDS_S,
       SATCHEL_SWITCH_IPC_SERVICE_FGM_0, SATCHEL_SWITCH_IPC_SERVICE_FGM_9, SATCHEL_SWITCH_IPC_SERVICE_NVDRV_S,
       SATCHEL_SWITCH_IPC_SERVICE_NVDRV_T, SATCHEL_SWITCH_IPC_SERVICE_MM_U, SATCHEL_SWITCH_IPC_SERVICE_CAPS_U,
       SATCHEL_SWITCH_IPC_SERVICE_CAPS_A, SATCHEL_SWITCH_IPC_SERVICE_CAPS_C, SATCHEL_SWITCH_IPC_SERVICE_CAPS_DC,
       SATCHEL_SWITCH_IPC_SERVICE_CAPS_SC, SATCHEL_SWITCH_IPC_SERVICE_CAPS_SU, SATCHEL_SWITCH_IPC_SERVICE_PREPO_U,
       SATCHEL_SWITCH_IPC_SERVICE_PREPO_S, SATCHEL_SWITCH_IPC_SERVICE_PREPO_M, SATCHEL_SWITCH_IPC_SERVICE_PREPO_A,
       SATCHEL_SWITCH_IPC_SERVICE_PREPO_A2, SATCHEL_SWITCH_IPC_SERVICE_PREPO_D, SATCHEL_SWITCH_IPC_SERVICE_PSC_C,
       SATCHEL_SWITCH_IPC_SERVICE_PSC_L,
       SATCHEL_SWITCH_IPC_SERVICE_ACC_AA,
       SATCHEL_SWITCH_IPC_SERVICE_ACC_SU,
       SATCHEL_SWITCH_IPC_SERVICE_ACC_U1,
       SATCHEL_SWITCH_IPC_SERVICE_AOC_U,
       SATCHEL_SWITCH_IPC_SERVICE_ARP_R,
       SATCHEL_SWITCH_IPC_SERVICE_ARP_W,
       SATCHEL_SWITCH_IPC_SERVICE_AUD_A,
       SATCHEL_SWITCH_IPC_SERVICE_AUD_D,
       SATCHEL_SWITCH_IPC_SERVICE_AUDCTL,
       SATCHEL_SWITCH_IPC_SERVICE_AUDOUT_A,
       SATCHEL_SWITCH_IPC_SERVICE_AUDOUT_D,
       SATCHEL_SWITCH_IPC_SERVICE_AUDREC_U,
       SATCHEL_SWITCH_IPC_SERVICE_AVM,
       SATCHEL_SWITCH_IPC_SERVICE_BCAT_A,
       SATCHEL_SWITCH_IPC_SERVICE_BCAT_M,
       SATCHEL_SWITCH_IPC_SERVICE_BCAT_S,
       SATCHEL_SWITCH_IPC_SERVICE_BCAT_U,
       SATCHEL_SWITCH_IPC_SERVICE_BGTC_SC,
       SATCHEL_SWITCH_IPC_SERVICE_BGTC_T,
       SATCHEL_SWITCH_IPC_SERVICE_BPC_R,
       SATCHEL_SWITCH_IPC_SERVICE_BSDCFG,
       SATCHEL_SWITCH_IPC_SERVICE_BT,
       SATCHEL_SWITCH_IPC_SERVICE_BTDRV,
       SATCHEL_SWITCH_IPC_SERVICE_BTM,
       SATCHEL_SWITCH_IPC_SERVICE_BTM_DBG,
       SATCHEL_SWITCH_IPC_SERVICE_BTM_SYS,
       SATCHEL_SWITCH_IPC_SERVICE_BTM_U,
       SATCHEL_SWITCH_IPC_SERVICE_CAPMTP,
       SATCHEL_SWITCH_IPC_SERVICE_CAPS_SS,
       SATCHEL_SWITCH_IPC_SERVICE_CEC_MGR,
       SATCHEL_SWITCH_IPC_SERVICE_CLKRST,
       SATCHEL_SWITCH_IPC_SERVICE_CLKRST_A,
       SATCHEL_SWITCH_IPC_SERVICE_CLKRST_I,
       SATCHEL_SWITCH_IPC_SERVICE_DAUTH_0,
       SATCHEL_SWITCH_IPC_SERVICE_ECTX_AW,
       SATCHEL_SWITCH_IPC_SERVICE_ECTX_R,
       SATCHEL_SWITCH_IPC_SERVICE_ECTX_W,
       SATCHEL_SWITCH_IPC_SERVICE_ERPT_C,
       SATCHEL_SWITCH_IPC_SERVICE_ERPT_R,
       SATCHEL_SWITCH_IPC_SERVICE_ES,
       SATCHEL_SWITCH_IPC_SERVICE_ETHC_C,
       SATCHEL_SWITCH_IPC_SERVICE_ETHC_I,
       SATCHEL_SWITCH_IPC_SERVICE_EUPLD_C,
       SATCHEL_SWITCH_IPC_SERVICE_EUPLD_R,
       SATCHEL_SWITCH_IPC_SERVICE_FAN,
       SATCHEL_SWITCH_IPC_SERVICE_FATAL_P,
       SATCHEL_SWITCH_IPC_SERVICE_FGM_DBG,
       SATCHEL_SWITCH_IPC_SERVICE_FRIEND_A,
       SATCHEL_SWITCH_IPC_SERVICE_FRIEND_M,
       SATCHEL_SWITCH_IPC_SERVICE_FRIEND_V,
       SATCHEL_SWITCH_IPC_SERVICE_FSP_LDR,
       SATCHEL_SWITCH_IPC_SERVICE_FSP_PR,
       SATCHEL_SWITCH_IPC_SERVICE_GPIO,
       SATCHEL_SWITCH_IPC_SERVICE_GRC_C,
       SATCHEL_SWITCH_IPC_SERVICE_GRC_D,
       SATCHEL_SWITCH_IPC_SERVICE_HID_DBG,
       SATCHEL_SWITCH_IPC_SERVICE_HID_SYS,
       SATCHEL_SWITCH_IPC_SERVICE_HIDBUS,
       SATCHEL_SWITCH_IPC_SERVICE_HSHL_SET,
       SATCHEL_SWITCH_IPC_SERVICE_HSHL_SYS,
       SATCHEL_SWITCH_IPC_SERVICE_HTCS,
       SATCHEL_SWITCH_IPC_SERVICE_HWOPUS,
       SATCHEL_SWITCH_IPC_SERVICE_I2C,
       SATCHEL_SWITCH_IPC_SERVICE_IDLE_SYS,
       SATCHEL_SWITCH_IPC_SERVICE_INS_R,
       SATCHEL_SWITCH_IPC_SERVICE_INS_S,
       SATCHEL_SWITCH_IPC_SERVICE_IRS,
       SATCHEL_SWITCH_IPC_SERVICE_IRS_SYS,
       SATCHEL_SWITCH_IPC_SERVICE_LBL,
       SATCHEL_SWITCH_IPC_SERVICE_LDN_M,
       SATCHEL_SWITCH_IPC_SERVICE_LDN_S,
       SATCHEL_SWITCH_IPC_SERVICE_LDN_U,
       SATCHEL_SWITCH_IPC_SERVICE_LDR_DMNT,
       SATCHEL_SWITCH_IPC_SERVICE_LDR_PM,
       SATCHEL_SWITCH_IPC_SERVICE_LDR_SHEL,
       SATCHEL_SWITCH_IPC_SERVICE_LP2P_APP,
       SATCHEL_SWITCH_IPC_SERVICE_LP2P_SYS,
       SATCHEL_SWITCH_IPC_SERVICE_LR,
       SATCHEL_SWITCH_IPC_SERVICE_MIG_USR,
       SATCHEL_SWITCH_IPC_SERVICE_MII_E,
       SATCHEL_SWITCH_IPC_SERVICE_MII_U,
       SATCHEL_SWITCH_IPC_SERVICE_MIIIMG,
       SATCHEL_SWITCH_IPC_SERVICE_MNPP_APP,
       SATCHEL_SWITCH_IPC_SERVICE_NCM,
       SATCHEL_SWITCH_IPC_SERVICE_NEWS_A,
       SATCHEL_SWITCH_IPC_SERVICE_NEWS_C,
       SATCHEL_SWITCH_IPC_SERVICE_NEWS_M,
       SATCHEL_SWITCH_IPC_SERVICE_NEWS_P,
       SATCHEL_SWITCH_IPC_SERVICE_NEWS_V,
       SATCHEL_SWITCH_IPC_SERVICE_NFC_AM,
       SATCHEL_SWITCH_IPC_SERVICE_NFC_MF_U,
       SATCHEL_SWITCH_IPC_SERVICE_NFC_SYS,
       SATCHEL_SWITCH_IPC_SERVICE_NFC_USER,
       SATCHEL_SWITCH_IPC_SERVICE_NFP_DBG,
       SATCHEL_SWITCH_IPC_SERVICE_NFP_SYS,
       SATCHEL_SWITCH_IPC_SERVICE_NFP_USER,
       SATCHEL_SWITCH_IPC_SERVICE_NGC_U,
       SATCHEL_SWITCH_IPC_SERVICE_NGCT_S,
       SATCHEL_SWITCH_IPC_SERVICE_NGCT_U,
       SATCHEL_SWITCH_IPC_SERVICE_NIM,
       SATCHEL_SWITCH_IPC_SERVICE_NIM_ECA,
       SATCHEL_SWITCH_IPC_SERVICE_NIM_ECAS,
       SATCHEL_SWITCH_IPC_SERVICE_NIM_SHP,
       SATCHEL_SWITCH_IPC_SERVICE_NOTIF_A,
       SATCHEL_SWITCH_IPC_SERVICE_NOTIF_S,
       SATCHEL_SWITCH_IPC_SERVICE_NPNS_S,
       SATCHEL_SWITCH_IPC_SERVICE_NPNS_U,
       SATCHEL_SWITCH_IPC_SERVICE_NS_DEV,
       SATCHEL_SWITCH_IPC_SERVICE_NS_EC,
       SATCHEL_SWITCH_IPC_SERVICE_NS_RID,
       SATCHEL_SWITCH_IPC_SERVICE_NS_RT,
       SATCHEL_SWITCH_IPC_SERVICE_NS_SU,
       SATCHEL_SWITCH_IPC_SERVICE_NS_VM,
       SATCHEL_SWITCH_IPC_SERVICE_NS_WEB,
       SATCHEL_SWITCH_IPC_SERVICE_NSD_A,
       SATCHEL_SWITCH_IPC_SERVICE_NSD_U,
       SATCHEL_SWITCH_IPC_SERVICE_NTC,
       SATCHEL_SWITCH_IPC_SERVICE_NVDRVDBG,
       SATCHEL_SWITCH_IPC_SERVICE_NVGEM_C,
       SATCHEL_SWITCH_IPC_SERVICE_NVGEM_CD,
       SATCHEL_SWITCH_IPC_SERVICE_OLSC_S,
       SATCHEL_SWITCH_IPC_SERVICE_OLSC_U,
       SATCHEL_SWITCH_IPC_SERVICE_OMM,
       SATCHEL_SWITCH_IPC_SERVICE_OVLN_RCV,
       SATCHEL_SWITCH_IPC_SERVICE_OVLN_SND,
       SATCHEL_SWITCH_IPC_SERVICE_PCIE,
       SATCHEL_SWITCH_IPC_SERVICE_PCIE_LOG,
       SATCHEL_SWITCH_IPC_SERVICE_PCM,
       SATCHEL_SWITCH_IPC_SERVICE_PCV,
       SATCHEL_SWITCH_IPC_SERVICE_PDM_NTFY,
       SATCHEL_SWITCH_IPC_SERVICE_PDM_QRY,
       SATCHEL_SWITCH_IPC_SERVICE_PGL,
       SATCHEL_SWITCH_IPC_SERVICE_PM_BM,
       SATCHEL_SWITCH_IPC_SERVICE_PM_DMNT,
       SATCHEL_SWITCH_IPC_SERVICE_PM_INFO,
       SATCHEL_SWITCH_IPC_SERVICE_PM_SHELL,
       SATCHEL_SWITCH_IPC_SERVICE_PSM,
       SATCHEL_SWITCH_IPC_SERVICE_RGLTR,
       SATCHEL_SWITCH_IPC_SERVICE_RO_DMNT,
       SATCHEL_SWITCH_IPC_SERVICE_RTC,
       SATCHEL_SWITCH_IPC_SERVICE_SET_CAL,
       SATCHEL_SWITCH_IPC_SERVICE_SET_FD,
       SATCHEL_SWITCH_IPC_SERVICE_SM_M,
       SATCHEL_SWITCH_IPC_SERVICE_SPSM,
       SATCHEL_SWITCH_IPC_SERVICE_SREPO_A,
       SATCHEL_SWITCH_IPC_SERVICE_SREPO_U,
       SATCHEL_SWITCH_IPC_SERVICE_TC,
       SATCHEL_SWITCH_IPC_SERVICE_TCAP,
       SATCHEL_SWITCH_IPC_SERVICE_TIME_A,
       SATCHEL_SWITCH_IPC_SERVICE_TIME_AL,
       SATCHEL_SWITCH_IPC_SERVICE_TIME_M,
       SATCHEL_SWITCH_IPC_SERVICE_TIME_P,
       SATCHEL_SWITCH_IPC_SERVICE_TIME_R,
       SATCHEL_SWITCH_IPC_SERVICE_TIME_SU,
       SATCHEL_SWITCH_IPC_SERVICE_TS,
       SATCHEL_SWITCH_IPC_SERVICE_UART,
       SATCHEL_SWITCH_IPC_SERVICE_USB_DS,
       SATCHEL_SWITCH_IPC_SERVICE_USB_HS,
       SATCHEL_SWITCH_IPC_SERVICE_USB_HS_A,
       SATCHEL_SWITCH_IPC_SERVICE_USB_OBSV,
       SATCHEL_SWITCH_IPC_SERVICE_USB_PD,
       SATCHEL_SWITCH_IPC_SERVICE_USB_PD_C,
       SATCHEL_SWITCH_IPC_SERVICE_USB_PD_M,
       SATCHEL_SWITCH_IPC_SERVICE_USB_PM,
       SATCHEL_SWITCH_IPC_SERVICE_USB_QDB,
       SATCHEL_SWITCH_IPC_SERVICE_VI_S,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN_DTC,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN_INF,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN_LCL,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN_LG,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN_LGA,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN_ND,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN_P,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN_SG,
       SATCHEL_SWITCH_IPC_SERVICE_WLAN_SOC,
       SATCHEL_SWITCH_IPC_SERVICE_XCD_SYS };
enum { SATCHEL_SWITCH_IPC_BLOCK_NONE, SATCHEL_SWITCH_IPC_BLOCK_PARSE, SATCHEL_SWITCH_IPC_BLOCK_SCHEMA,
       SATCHEL_SWITCH_IPC_BLOCK_COMMAND, SATCHEL_SWITCH_IPC_BLOCK_SERVICE, SATCHEL_SWITCH_IPC_BLOCK_TLS,
       SATCHEL_SWITCH_IPC_BLOCK_RESOURCE, SATCHEL_SWITCH_IPC_BLOCK_BUFFER, SATCHEL_SWITCH_IPC_BLOCK_OBJECT, SATCHEL_SWITCH_IPC_BLOCK_STORAGE, SATCHEL_SWITCH_IPC_BLOCK_HANDLE };
enum { SATCHEL_SWITCH_IPC_OBJECT_NONE, SATCHEL_SWITCH_IPC_OBJECT_SM, SATCHEL_SWITCH_IPC_OBJECT_LM, SATCHEL_SWITCH_IPC_OBJECT_LOGGER, SATCHEL_SWITCH_IPC_OBJECT_FSP, SATCHEL_SWITCH_IPC_OBJECT_STORAGE, SATCHEL_SWITCH_IPC_OBJECT_APM, SATCHEL_SWITCH_IPC_OBJECT_APM_SESSION,
       SATCHEL_SWITCH_IPC_OBJECT_APPLET, SATCHEL_SWITCH_IPC_OBJECT_APPLICATION_PROXY,
       SATCHEL_SWITCH_IPC_OBJECT_COMMON_STATE, SATCHEL_SWITCH_IPC_OBJECT_SELF_CONTROLLER,
       SATCHEL_SWITCH_IPC_OBJECT_WINDOW_CONTROLLER, SATCHEL_SWITCH_IPC_OBJECT_AUDIO_CONTROLLER,
       SATCHEL_SWITCH_IPC_OBJECT_DISPLAY_CONTROLLER, SATCHEL_SWITCH_IPC_OBJECT_LIBRARY_APPLET_CREATOR,
       SATCHEL_SWITCH_IPC_OBJECT_APPLICATION_FUNCTIONS, SATCHEL_SWITCH_IPC_OBJECT_DEBUG_FUNCTIONS, SATCHEL_SWITCH_IPC_OBJECT_AOC, SATCHEL_SWITCH_IPC_OBJECT_ECTX_APPLICATION,
       SATCHEL_SWITCH_IPC_OBJECT_PCTL, SATCHEL_SWITCH_IPC_OBJECT_PCTL_CHILD,
       SATCHEL_SWITCH_IPC_OBJECT_PCTL_SYSTEM, SATCHEL_SWITCH_IPC_OBJECT_PCTL_SYSTEM_CHILD,
       SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN, SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN_CHILD,
       SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY, SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY_CHILD,
       SATCHEL_SWITCH_IPC_OBJECT_TIME_USER, SATCHEL_SWITCH_IPC_OBJECT_TIME_USER_CLOCK,
       SATCHEL_SWITCH_IPC_OBJECT_TIME_NETWORK_CLOCK, SATCHEL_SWITCH_IPC_OBJECT_TIME_STEADY_CLOCK,
       SATCHEL_SWITCH_IPC_OBJECT_TIME_ZONE, SATCHEL_SWITCH_IPC_OBJECT_TIME_LOCAL_CLOCK };
typedef struct {
    uint32_t parser_status, kind, command_id, service_kind, blocked_reason;
    uint32_t wire_type, receive_list_mode, receive_list_offset, reserved_header_bits;
    uint32_t raw_word_count, has_special_header, object_kind;
    uint32_t storage_plan_status, storage_status, storage_write_status;
} SatchelSwitchIPCInfo;
typedef struct {
    const uint8_t *bytes;
    size_t size;
} SatchelSwitchDispatchCode;
typedef struct {
    uint8_t focus_state, operation_mode, foreground_rights;
    uint8_t operation_notifications, performance_notifications;
    uint8_t focus_handling[3], focus_handling_set;
    uint8_t out_of_focus_suspending, out_of_focus_suspending_set;
} SatchelSwitchAppletState;
typedef struct {
    uint64_t application_id, add_on_content_base_id;
    uint32_t application_version, parental_control_flags;
    int8_t rating_ages[32];
} SatchelSwitchApplicationControl;
enum { SATCHEL_SWITCH_PARENTAL_POLICY_UNCONFIGURED = 0 };
typedef struct {
    uint32_t attached, parental_policy;
    uint64_t attachment_count;
} SatchelSwitchApplicationControlInfo;
/* Caller supplies already verified CNMT-linked numeric metadata. This boundary
 * checks NPDM identity and copies56 bytes; it does not authenticate an archive.
 * Exactly one attachment is allowed before any run call. No policy decision or
 * Initialize command follows from attachment; unknown flag bits stay opaque. */
int satchel_switch_process_attach_application_control(SatchelSwitchProcess *process, const SatchelSwitchApplicationControl *metadata);
/* Safe presence/counters only; null failure leaves caller output unchanged. */
int satchel_switch_process_application_control_info(const SatchelSwitchProcess *process, SatchelSwitchApplicationControlInfo *out);

enum { SATCHEL_SWITCH_PARENTAL_CHILD_LIMIT = 32 };
typedef struct {
    uint32_t live_children, initialized_children, metadata_bound_children, policy;
    uint64_t created, initialize_requests, initialize_serviced;
} SatchelSwitchParentalInfo;
/* Child-kind enum numbers are historical identities, not initialization state.
 * Shared records survive domain clones and release with the final owning object.
 * Initialize binds verified numeric metadata; policy stays UNCONFIGURED. No
 * restriction approval, private publication/event or downstream API is implied. */
int satchel_switch_process_parental_info(const SatchelSwitchProcess *process, SatchelSwitchParentalInfo *out);

/* Accepted HIPC Close2 releases its server endpoint/domain reference and
 * returns kernel SessionClosed0xF601 without a reply/TLS write. The closed
 * client token occupies its slot until SVC CloseHandle. Domain clones retain
 * independent endpoints sharing the object table; closure does not kill peers.
 * No generic handle-duplication or session-wait support is implied. */
uint64_t satchel_switch_process_session_closes(const SatchelSwitchProcess *process);
uint32_t satchel_switch_process_closed_sessions(const SatchelSwitchProcess *process);
int satchel_switch_process_is_session_closed(const SatchelSwitchProcess *process, uint32_t handle);

enum { SATCHEL_SWITCH_PERFORMANCE_NORMAL = 0 };
enum { SATCHEL_SWITCH_APPLET_CREATED = 0, SATCHEL_SWITCH_APPLET_FOREGROUND = 1, SATCHEL_SWITCH_APPLET_MESSAGE_LIMIT = 8 };
typedef struct {
    uint32_t phase, pending;
    uint64_t enqueued, received;
} SatchelSwitchAppletMessageInfo;
/* Explicit host user-launch transition, before any guest instruction. Created
 * profile is virtual Background3; activation sets InFocus1 and queues15 then1
 * once. Repeated activation is an idempotent no-op. It does not resume an iOS
 * backgrounded diagnostic or signal GPU/suspension/mode changes. */
int satchel_switch_process_begin_foreground_launch(SatchelSwitchProcess *process);
int satchel_switch_process_applet_message_info(const SatchelSwitchProcess *process, SatchelSwitchAppletMessageInfo *out);

enum { SATCHEL_SWITCH_PROCESS_MODULE_LIMIT = 8, SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT = 256 * 1024 * 1024,
       SATCHEL_SWITCH_PROCESS_STACK_LIMIT = 16 * 1024 * 1024, SATCHEL_SWITCH_PROCESS_DISPATCH_LIMIT = 64 * 1024, SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT = 32,
       SATCHEL_SWITCH_LOG_PACKET_LIMIT = 16 * 1024, SATCHEL_SWITCH_LOG_PACKET_SLOTS = 4, SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT = 32 };
/* Virtual alias bytes share existing backing and are not charged twice.
 * Requests include errors; serviced counts include only committed mappings. */
/* SVC3 metadata-only PermissionLocked support. Successful mask-zero/repeated
 * locks count; locked bytes count current owned coverage once, not allocations.
 * Valid Uncached requests stop until cache/backing semantics are implemented. */
uint64_t satchel_switch_process_attribute_requests(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_attribute_serviced(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_permission_locked_bytes(const SatchelSwitchProcess *process);
/* SVC2 counters: successes include validated unchanged requests. Changed bytes
 * are cumulative virtual coverage; no physical backing is allocated or freed. */
uint64_t satchel_switch_process_permission_requests(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_permission_serviced(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_permission_changed_bytes(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_alias_memory_size(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_alias_map_requests(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_alias_unmap_requests(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_alias_map_serviced(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_alias_unmap_serviced(const SatchelSwitchProcess *process);
int satchel_switch_process_metadata_supported(const SatchelProcessMetadata *metadata);

/* Bounded NSO process diagnostic with the explicitly described kernel/IPC slice.
 * Copies decoded modules; caller retains ownership of its input buffers.
 * QueryMemory, GetInfo(RandomEntropy), and validated 36/39-bit virtual-region
 * queries are serviced. No heap pages are allocated just by reserving its range.
 * Initial-thread priority/affinity/ID queries use owned thread state. Unsupported
 * requests stop. Core/priority-mask queries require validated NPDM capability
 * metadata. Non-system memory queries21/22 use a fixed emulator budget with
 * NPDM system-resource bytes and unbacked NSO holes withheld; usage is CPU-backed
 * page-rounded segments, stack, TLS and dispatch bytes, not host RSS. System
 * resource queries16/17 report actual owned arena capacity/used pages. The arena
 * stores live process mapping metadata; it is not a hardware page table or host
 * RSS measurement. Zero resource reports0. The explicit normal-application
 * profile charges the full arena capacity for total memory queries6/7, matching
 * the pinned non-applet secure-size rule. Applet/system memory-pool selection
 * and platform allocation overhead are not modeled or inferred from NPDM.
 * SetHeapSize creates/resizes/removes real zero-initialized guest RW backing
 * within that budget. Map/UnmapPhysicalMemory own zeroed Normal RW backing in
 * the alias reservation, filling free gaps and supporting partial unmaps with
 * atomic metadata/accounting updates. Only newly added pages consume budget.
 * MapMemory4/UnmapMemory5 retain real backing views: eligible source CodeData/
 * Normal pages become user-inaccessible/Locked, destination becomes Stack/RW,
 * and matched partial unmap restores source access without copying bytes.
 * Alias views do not increase physical usage. Resizing retained allocations
 * beyond supported CPU operations stops explicitly. No page borrowing or
 * arbitrary SetMemoryAttribute operation is modeled.
 * ConnectToNamedPort recognizes only the owned emulated sm: port. Sessions are
 * opaque live objects. SM Initialize binds the owned client identity; GetService
 * validates client/name and creates registered LM objects. FSP is registered
 * only with an attached verified storage provider: Initialize1 and current-
 * program OpenDataStorage200. IStorage supports Read0/GetSize4 only. Reads
 * validate the full writable mapping and exclude the TLS page, fetch verified
 * host staging, then atomically commit data plus reply. APM owns real root/
 * session objects and reports emulated handheld/Normal mode; configuration
 * methods remain unsupported. No host power or clock settings are changed.
 * appletOE creates process-bound application proxies and typed controller
 * interfaces; WindowController1 returns a process-owned resource identity.
 * CommonStateGetter900 owns the exit-to-library policy; ExecuteProgram still
 * stops. GetEventHandle0 returns a retained COPY-readable manual queue event;
 * ReceiveMessage1 dequeues an actual notification, or AM NoMessages0x680.
 * Explicit host launch transitions virtual Background3 to InFocus1 and queues
 * FocusStateChanged15/ChangeIntoForeground1 once. Suspension/GPU events remain
 * separate. The handheld profile owns foreground-rights/focus/notification
 * policies; running/logo/audio/display methods remain unsupported. appletAE and
 * other services stop. LM/ILogger retain validated mapped-input packets in a private bounded
 * sink. No log payload is exported; pointer-buffer capacity is0. CloseHandle drops
 * an owned reference; current-thread pseudo-handle retains its thread object.
 * LM/logger sessions can convert to bounded domains. Typed object IDs are
 * scoped to the retained domain table; close removes only the selected ID.
 * Unknown IDs used for dispatch stop; domain close is idempotent. Control2
 * domain clones share a retained table. No non-domain/tagged clone, copy-from-
 * domain, nested conversion or server pointer buffer is implemented.
 * Thread execution is serialized across admitted virtual cores with bounded
 * priority quanta,32 concurrent recyclable records and packed0x200 TLS slots.
 * CreateThread copies only known FP status and initializes fresh registers;
 * event/thread waits retain real objects and can return explicit BLOCKED.
 * Migration, priority/affinity mutation and broader scheduler services stop.
 * The final existing exited context retains shared RAM until Process destroy;
 * it is not runnable and is not an extra anchor context. */
SatchelSwitchProcess *satchel_switch_process_create(const SatchelSwitchProcessModule *modules, size_t count,
    const SatchelProcessMetadata *metadata, char *error, size_t capacity);
/* Optional original emulator-owned dispatch code, copied into a separate RX
 * GeneratedCode (0x14) mapping after the unchanged NSO span. It remains fully
 * visible to QueryMemory. This does not implement graphics or CodeMemory SVCs.
 * A non-null config requires 4..64KiB of caller-owned readable, 4-byte-sized
 * code. Its page-rounded length counts against the same process memory limit. */
SatchelSwitchProcess *satchel_switch_process_create_with_dispatch(const SatchelSwitchProcessModule *modules, size_t count,
    const SatchelProcessMetadata *metadata, const SatchelSwitchDispatchCode *dispatch, char *error, size_t capacity);
/* Opt-in opaque HLE identity namespace. This is real private guest backing,
 * visible as Reserved (0x10), NONE; never guest-readable/writable/executable.
 * The page is immutable through Process guest SVC policy, not a CPU intrinsic.
 * Existing constructors allocate no identity page and keep their old behavior. */
enum { SATCHEL_SWITCH_IDENTITY_VERSION = 1, SATCHEL_SWITCH_IDENTITY_SIZE = 4096 };
typedef struct {
    uint32_t version, size;
    uint64_t process_id, generation, address, length;
} SatchelSwitchIdentityReservation;
SatchelSwitchProcess *satchel_switch_process_create_with_dispatch_and_identity(const SatchelSwitchProcessModule *modules, size_t count,
    const SatchelProcessMetadata *metadata, const SatchelSwitchDispatchCode *dispatch, char *error, size_t capacity);
/* Descriptor copy only; does not attest current CPU permissions. Failure leaves
 * out unchanged. Call the validator before each borrowed use. */
int satchel_switch_process_identity_reservation(const SatchelSwitchProcess *, SatchelSwitchIdentityReservation *out);
/* Serialized borrowed snapshot; Process/CPU/descriptor must remain alive for
 * the call. CPU must be one of this Process's retained contexts, outside run
 * and callbacks. Exact descriptor/map metadata plus the frozen CPU's NONE->NONE
 * preflight prove current NONE, no aliases/locks/shared pages and original RW
 * provenance. That exact no-op allocates nothing and preserves registers,
 * mappings and exclusive reservations. Initial CPU backing cannot be removed
 * or replaced by existing CPU APIs. Trusted host permission/alias changes are
 * possible and cause refusal; this is not a mapping lease or historical audit.
 * No callback, CPU pointer or external descriptor is retained. */
int satchel_switch_process_validate_identity(SatchelSwitchProcess *, SatchelCPUProbe *, const SatchelSwitchIdentityReservation *);
/* Constructor-only consumer claim, after private owner allocation/preflight.
 * Validates then burns this namespace once; no reset/release, including after
 * consumer destruction. Rejected claims change nothing. A successful caller
 * must publish without fallible work. Same serialized lifetime contract. */
int satchel_switch_process_claim_identity(SatchelSwitchProcess *, SatchelCPUProbe *, const SatchelSwitchIdentityReservation *);
/* Current-context identity only. Version 1 is exactly 24 bytes; generation is
 * the globally non-reused thread ID, never a pointer or recycled slot number. */
enum { SATCHEL_SWITCH_CONTEXT_IDENTITY_VERSION = 1 };
typedef struct {
    uint32_t version, size;
    uint64_t process_id, context_generation;
} SatchelSwitchContextIdentity;
/* Serialized borrowed calls: the Process and descriptor/output remain alive;
 * the supplied CPU is compared with owned records before it is dereferenced.
 * Only the current, retained RUNNABLE context is accepted, outside active CPU
 * execution (including a sibling sharing memory). Failure preserves output.
 * These calls allocate nothing, retain nothing and preserve CPU diagnostics,
 * registers, memory and exclusive reservations. They do NOT attest a fresh
 * breakpoint, a particular PC/LR, or nonterminal usability. A provisional
 * set_pc(LR) does not invalidate identity; stop/return checks remain separate.
 * After allocation-address reuse the getter identifies the NEW context; retain
 * and validate the numeric descriptor to reject an expired context generation.
 * No borrowed pointer or descriptor extends Process/context lifetime. */
int satchel_switch_process_context_identity(const SatchelSwitchProcess *, SatchelCPUProbe *, SatchelSwitchContextIdentity *out);
int satchel_switch_process_validate_context(const SatchelSwitchProcess *, SatchelCPUProbe *, const SatchelSwitchContextIdentity *);
/* One immutable read-only provider descriptor, copied before the first run.
 * No ownership transfer: provider context/backing must outlive process destroy.
 * Null callback, unsigned-only size, replacement or late attachment rejects. */
int satchel_switch_process_attach_storage(SatchelSwitchProcess *process, const SatchelSwitchStorage *storage);
uint64_t satchel_switch_process_storage_reads(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_storage_bytes(const SatchelSwitchProcess *process);
void satchel_switch_process_destroy(SatchelSwitchProcess *process);
SatchelCPUProbe *satchel_switch_process_cpu(SatchelSwitchProcess *process);
int satchel_switch_process_query(SatchelSwitchProcess *process, uint64_t address, SatchelSwitchMemoryInfo *info);
SatchelCPUProbeResult satchel_switch_process_run(SatchelSwitchProcess *process, uint64_t budget,
    SatchelCPUProbeCancelled cancelled, void *context);
uint64_t satchel_switch_process_queries(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_info_requests(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_info_serviced(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_thread_queries(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_system_resource_total(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_system_resource_used(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_physical_memory_size(const SatchelSwitchProcess *process);
/* Accepted calls include real unchanged-state no-ops; requests also count errors. */
uint64_t satchel_switch_process_physical_map_serviced(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_physical_unmap_serviced(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_physical_map_requests(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_physical_unmap_requests(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_heap_requests(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_heap_size(const SatchelSwitchProcess *process);
/* Typed lookup for future IPC dispatch: only live owned SM sessions qualify. */
int satchel_switch_process_is_sm_session(const SatchelSwitchProcess *process, uint32_t handle);
uint64_t satchel_switch_process_sm_sessions(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_port_requests(const SatchelSwitchProcess *process);
int satchel_switch_process_is_sm_initialized(const SatchelSwitchProcess *process, uint32_t handle);
uint64_t satchel_switch_process_ipc_requests(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_ipc_serviced(const SatchelSwitchProcess *process);
/* Safe protocol fields only; no name bytes, guest payload, claimed PID or keys.
 * Output unchanged before the first owned-session IPC or on invalid arguments. */
int satchel_switch_process_last_ipc(const SatchelSwitchProcess *process, SatchelSwitchIPCInfo *info);
/* Static public allowlist label only; never returns a guest-supplied string.
 * Families with several endpoint names use the first canonical listed label. */
const char *satchel_switch_process_service_name(uint32_t service_kind);
/* Private bounded LM sink: no API exposes retained packet contents. */
int satchel_switch_process_is_lm_session(const SatchelSwitchProcess *process, uint32_t handle);
// Domain IDs are scoped to the owning shared session, not kernel handles.
// At most32 live objects; released IDs are not reused within that session.
int satchel_switch_process_is_domain_session(const SatchelSwitchProcess *process, uint32_t handle);
uint32_t satchel_switch_process_domain_objects(const SatchelSwitchProcess *process, uint32_t handle);
int satchel_switch_process_is_logger(const SatchelSwitchProcess *process, uint32_t handle);
/* Explicit emulated handheld/Normal profile; not host power/frequency state.
 * UINT32_MAX for NULL. No performance configuration IDs are invented. */
/* Process-owned applet resource identity; separate from guest handle/PID space. */
int satchel_switch_process_applet_exit_to_library_enabled(const SatchelSwitchProcess *process);
int satchel_switch_process_is_readable_event(const SatchelSwitchProcess *process, uint32_t handle);
uint64_t satchel_switch_process_readable_event_handles(const SatchelSwitchProcess *process);
/* -1 means no event exists; otherwise the actual manual-event signal level. */
int satchel_switch_process_applet_event_signaled(const SatchelSwitchProcess *process);
/* Accumulated guest suspension, distinct from host idle/background time.
 * This bounded profile has no suspend/resume transition; cancellation ends it.
 * The change event is distinct from the applet message event; absent=-1. */
/* Explicit sole-application foreground/handheld profile. State is shared across
 * proxy/controller handles. NULL arguments leave output unchanged. No host
 * focus, performance or power controls are modified. */
int satchel_switch_process_applet_state(const SatchelSwitchProcess *process, SatchelSwitchAppletState *out);
/* Only a proven fatal error in the guest renderer should call this serialized
 * API. It is sticky and signals the GPU-error event if present. Admission,
 * allocation, unsupported calls and independent fixture failures are not GPU
 * fault reports. No host or guest payload is accepted or exposed. */
int satchel_switch_process_report_gpu_error(SatchelSwitchProcess *process);
int satchel_switch_process_gpu_error_detected(const SatchelSwitchProcess *process);
int satchel_switch_process_gpu_error_event_signaled(const SatchelSwitchProcess *process);
int satchel_switch_process_is_gpu_error_event(const SatchelSwitchProcess *process, uint32_t handle);
uint64_t satchel_switch_process_suspended_ticks(const SatchelSwitchProcess *process);
int satchel_switch_process_is_suspended_tick_event(const SatchelSwitchProcess *process, uint32_t handle);
int satchel_switch_process_suspended_tick_event_signaled(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_applet_resource_id(const SatchelSwitchProcess *process);
uint32_t satchel_switch_process_performance_mode(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_log_packets(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_log_retained_bytes(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_log_dropped_packets(const SatchelSwitchProcess *process);
uint32_t satchel_switch_process_log_destination(const SatchelSwitchProcess *process);
/* Safe request metadata only; never returns/logs process entropy. Outputs are
 * unchanged on null arguments or before the first GetInfo request. */
int satchel_switch_process_last_info(const SatchelSwitchProcess *process, uint32_t *type,
    uint32_t *handle, uint64_t *sub_id);
uint64_t satchel_switch_process_module_address(const SatchelSwitchProcess *process, size_t index);
/* Zero when no dispatch mapping exists; size is the actual page-rounded map. */
uint64_t satchel_switch_process_dispatch_address(const SatchelSwitchProcess *process);
uint64_t satchel_switch_process_dispatch_size(const SatchelSwitchProcess *process);
#endif
