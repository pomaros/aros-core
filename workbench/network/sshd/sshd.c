/*
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * Copyright (C) 2026 John KNIPPER.
 * Author: Fabian Schmieder (@metaneutrons)
 *
 * AROS SSH Server (sshd) using libssh 0.12.0 + mbedTLS 3.6.6
 *
 * Features:
 *   - Host key generation on first run (RSA 2048)
 *   - Public-key auth (ENVARC:SSH/authorized_keys, OpenSSH format)
 *   - Password auth (SHA-256 hash in ENVARC:SSH/password)
 *   - Concurrent sessions via CreateNewProc
 *   - Shell bridge via PIPE: handler
 *   - CTRL-C signal to shutdown
 */

/* bsdsocket bases are per task: the main task and every session task open
 * their own, and all socket calls (ours and libssh's) resolve the base
 * through __libssh_SocketBase() below */
struct Library;
struct Library *__libssh_SocketBase(void);
#define SocketBase (__libssh_SocketBase())

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/socket.h>
#include <proto/netservice.h>
#include <libraries/bsdsocket.h>
#include <exec/types.h>
#include <dos/dostags.h>
#include <dos/dos.h>
#include <dos/pipe.h>
#include <libraries/netservice.h>
#include <utility/tagitem.h>

#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>

#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>
#include <mbedtls/error.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char __attribute__((used)) verstag[] = "\0$VER: sshd 0.1 (19.8.2026)\r\n";

#define SSHD_PORT           22
#define SSHD_HOST_KEY       "ENVARC:SSH/host_key"
#define SSHD_AUTH_KEYS      "ENVARC:SSH/authorized_keys"
#define SSHD_PASSWORD_FILE  "ENVARC:SSH/password"
#define SSHD_CONFIG_DIR     "ENVARC:SSH"
#define SSHD_BANNER         "AROS SSH Server 0.1"
#define RSA_KEY_BITS        2048
#define MAX_LINE            1024
#define BUF_SIZE            4096

/* Each listener or session is a separate DOS program with its own runtime. */
static struct Library *DaemonSocketBase;

struct Library *__libssh_SocketBase(void)
{
    return DaemonSocketBase;
}
struct Library *NetServicesBase;        /* optional: set when run as a managed service */
static volatile BOOL g_shutdown = FALSE;

/* --- Utility: SHA-256 hex digest --- */
static void sha256_hex(const char *input, char *output)
{
    unsigned char hash[32];
    mbedtls_sha256((const unsigned char *)input, strnlen(input, 1024), hash, 0);

    int i;
    for (i = 0; i < 32; i++)
        sprintf(output + (i * 2), "%02x", hash[i]);
    output[64] = '\0';
}

/* --- Host key generation using mbedTLS --- */
static BOOL generate_host_key(void)
{
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char pers[] = "aros_sshd_keygen";
    unsigned char buf[16384];
    int ret;
    BPTR fh;

    Printf("sshd: Generating RSA %ld-bit host key...\n", (long)RSA_KEY_BITS);

    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 (const unsigned char *)pers, sizeof(pers) - 1);
    if (ret != 0) goto fail;

    ret = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) goto fail;

    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random,
                               &ctr_drbg, RSA_KEY_BITS, 65537);
    if (ret != 0) goto fail;

    /* Write PEM to buffer */
    memset(buf, 0, sizeof(buf));
    ret = mbedtls_pk_write_key_pem(&pk, buf, sizeof(buf));
    if (ret != 0) goto fail;

    /* Ensure config directory exists */
    BPTR lock = CreateDir(SSHD_CONFIG_DIR);
    if (lock) UnLock(lock);

    /* Save to file */
    fh = Open(SSHD_HOST_KEY, MODE_NEWFILE);
    if (!fh) goto fail;

    Write(fh, buf, strnlen((char *)buf, sizeof(buf)));
    Close(fh);

    Printf("sshd: Host key saved to %s\n", (IPTR)SSHD_HOST_KEY);

    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return TRUE;

fail:
    Printf("sshd: Failed to generate host key (mbedtls error %ld)\n", (long)ret);
    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return FALSE;
}

/* --- Ensure host key exists --- */
static BOOL ensure_host_key(void)
{
    BPTR lock = Lock(SSHD_HOST_KEY, SHARED_LOCK);
    if (lock)
    {
        UnLock(lock);
        return TRUE;
    }
    return generate_host_key();
}

/* Per-session state handed to the auth callbacks as userdata; libssh has
 * no server-side "is authenticated" query. */
struct SessionCtx {
    int authenticated;
    ssh_channel channel;
    BOOL started;
    char *command;
};

/* --- Public key authentication callback --- */
static int auth_pubkey_cb(ssh_session session, const char *user,
                          struct ssh_key_struct *pubkey,
                          char signature_state, void *userdata)
{
    struct SessionCtx *ctx = userdata;

    (void)session;

    if (strcmp(user, "root") != 0)
        return SSH_AUTH_DENIED;

    /* Probe phase: accept any key type for further verification */
    if (signature_state == SSH_PUBLICKEY_STATE_NONE)
        return SSH_AUTH_SUCCESS;

    if (signature_state != SSH_PUBLICKEY_STATE_VALID)
        return SSH_AUTH_DENIED;

    /* Verify key against authorized_keys */
    BPTR fh = Open(SSHD_AUTH_KEYS, MODE_OLDFILE);
    if (!fh)
        return SSH_AUTH_DENIED;

    char line[MAX_LINE];
    BOOL found = FALSE;

    while (FGets(fh, line, sizeof(line)))
    {
        /* Strip newline */
        int len = strnlen(line, sizeof(line));
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0')
            continue;

        /* OpenSSH format: "type base64 comment" - extract base64 part */
        char *space1 = strchr(line, ' ');
        if (!space1) continue;

        char *b64_start = space1 + 1;
        char *space2 = strchr(b64_start, ' ');
        if (space2) *space2 = '\0';

        /* Determine key type from prefix */
        enum ssh_keytypes_e ktype = SSH_KEYTYPE_UNKNOWN;
        if (strncmp(line, "ssh-rsa", 7) == 0)
            ktype = SSH_KEYTYPE_RSA;
        else if (strncmp(line, "ssh-ed25519", 11) == 0)
            ktype = SSH_KEYTYPE_ED25519;
        else if (strncmp(line, "ecdsa-sha2", 10) == 0)
            ktype = SSH_KEYTYPE_ECDSA_P256;

        ssh_key ref_key = NULL;
        if (ssh_pki_import_pubkey_base64(b64_start, ktype, &ref_key) == SSH_OK)
        {
            if (ssh_key_cmp(pubkey, ref_key, SSH_KEY_CMP_PUBLIC) == 0)
                found = TRUE;
            ssh_key_free(ref_key);
        }

        if (found) break;
    }

    Close(fh);
    if (found)
        ctx->authenticated = 1;
    return found ? SSH_AUTH_SUCCESS : SSH_AUTH_DENIED;
}

/* --- Password authentication callback --- */
static int auth_password_cb(ssh_session session, const char *user,
                            const char *password, void *userdata)
{
    struct SessionCtx *ctx = userdata;

    (void)session;

    if (strcmp(user, "root") != 0)
        return SSH_AUTH_DENIED;

    /* Password auth only if password file exists */
    BPTR fh = Open(SSHD_PASSWORD_FILE, MODE_OLDFILE);
    if (!fh)
        return SSH_AUTH_DENIED;

    char stored_hash[65] = {0};
    Read(fh, stored_hash, 64);
    Close(fh);
    stored_hash[64] = '\0';

    /* Strip any trailing whitespace */
    int len = strnlen(stored_hash, sizeof(stored_hash));
    while (len > 0 && (stored_hash[len-1] == '\n' || stored_hash[len-1] == '\r' || stored_hash[len-1] == ' '))
        stored_hash[--len] = '\0';

    /* Hash the provided password and compare */
    char computed_hash[65];
    sha256_hex(password, computed_hash);

    if (strcmp(computed_hash, stored_hash) == 0)
    {
        ctx->authenticated = 1;
        return SSH_AUTH_SUCCESS;
    }

    return SSH_AUTH_DENIED;
}

/* --- Shell session context --- */
struct CommandExit {
    struct Message message;
    LONG status;
};

static void command_exited(IPTR rc, IPTR userdata)
{
    struct CommandExit *exit = (struct CommandExit *)userdata;
    exit->status = (LONG)rc;
    PutMsg(exit->message.mn_ReplyPort, &exit->message);
}

struct ShellBridge {
    BPTR        shell_in;   /* our write end -> shell reads from this pipe */
    BPTR        shell_out;  /* our read end  <- shell writes to this pipe */
};

/* --- Unique pipe name generator --- */
static ULONG g_pipe_seq = 0;

static void make_pipe_name(char *buf, size_t len, const char *tag)
{
    snprintf(buf, len, "PIPE:sshd_%s_%p_%lx/32768", tag, (void *)FindTask(NULL), (unsigned long)++g_pipe_seq);
}

/* --- Launch shell with pipe I/O --- */
static BOOL launch_shell(struct ShellBridge *sb, struct CommandExit *exit)
{
    char in_name[64], out_name[64];

    make_pipe_name(in_name, sizeof(in_name), "in");
    make_pipe_name(out_name, sizeof(out_name), "out");

    /* Open write end of stdin pipe (we write, shell reads) */
    sb->shell_in = Open(in_name, MODE_NEWFILE);
    if (!sb->shell_in)
        return FALSE;

    /* Create the shell's stdout writer before opening our reader. */
    BPTR shell_stdout = Open(out_name, MODE_NEWFILE);
    if (!shell_stdout)
    {
        Close(sb->shell_in);
        sb->shell_in = BNULL;
        return FALSE;
    }

    /* Open the other ends for the shell process */
    BPTR shell_stdin = Open(in_name, MODE_OLDFILE);
    sb->shell_out = Open(out_name, MODE_OLDFILE);

    if (!shell_stdin || !sb->shell_out)
    {
        if (shell_stdin) Close(shell_stdin);
        Close(shell_stdout);
        Close(sb->shell_in);
        if (sb->shell_out) Close(sb->shell_out);
        sb->shell_in = BNULL;
        sb->shell_out = BNULL;
        return FALSE;
    }

    /* Launch C:Shell asynchronously */
    struct FileHandle *reader = BADDR(sb->shell_out);
    if (!DoPkt(reader->fh_Type, ACTION_PIPE_SET_NONBLOCK, reader->fh_Arg1, TRUE, 0, 0, 0))
    {
        Close(shell_stdin);
        Close(shell_stdout);
        Close(sb->shell_in);
        Close(sb->shell_out);
        return FALSE;
    }
    LONG rc = SystemTags("",
        SYS_Input,      (IPTR)shell_stdin,
        SYS_Output,     (IPTR)shell_stdout,
        SYS_Asynch,     TRUE,
        SYS_UserShell,  TRUE,
        SYS_Background, FALSE,
        NP_ExitCode, (IPTR)command_exited, NP_ExitData, (IPTR)exit,
        TAG_DONE);

    if (rc == -1)
    {
        Close(shell_stdin);
        Close(shell_stdout);
        Close(sb->shell_in);
        if (sb->shell_out) Close(sb->shell_out);
        sb->shell_in = BNULL;
        sb->shell_out = BNULL;
        return FALSE;
    }

    return TRUE;
}

/* --- Bidirectional shell bridge with PTY emulation --- */
static void handle_shell(ssh_channel channel)
{
    struct ShellBridge sb = {0};
    struct CommandExit exit = {0};
    exit.message.mn_ReplyPort = CreateMsgPort();

    if (!exit.message.mn_ReplyPort || !launch_shell(&sb, &exit))
    {
        if (exit.message.mn_ReplyPort) DeleteMsgPort(exit.message.mn_ReplyPort);
        ssh_channel_write(channel, "Error: cannot start shell\r\n", 27);
        ssh_channel_request_send_exit_status(channel, RETURN_FAIL);
        return;
    }

    char buf[BUF_SIZE];

    while (ssh_channel_is_open(channel))
    {
        LONG n = Read(sb.shell_out, buf, sizeof(buf));
        if (n == 0 || (n < 0 && IoErr() != ERROR_PIPE_WOULD_BLOCK)) break;
        for (LONG pos = 0; pos < n;)
        {
            int written = ssh_channel_write(channel, buf + pos, n - pos);
            if (written <= 0) goto close_shell;
            pos += written;
        }

        if (sb.shell_in)
        {
            int count = ssh_channel_read_nonblocking(channel, buf, sizeof(buf), 0);
            if (count > 0)
            {
                if (Write(sb.shell_in, buf, count) != count) break;
            }
            else if (count == SSH_ERROR) break;
            else if (ssh_channel_is_eof(channel))
            {
                Close(sb.shell_in);
                sb.shell_in = BNULL;
            }
            if (n < 0 && count <= 0) Delay(1);
        }
        else if (n < 0) Delay(1);
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) break;
    }

close_shell:
    if (sb.shell_in) Close(sb.shell_in);
    Close(sb.shell_out);
    WaitPort(exit.message.mn_ReplyPort);
    GetMsg(exit.message.mn_ReplyPort);
    DeleteMsgPort(exit.message.mn_ReplyPort);
    ssh_channel_request_send_exit_status(channel, exit.status);
}

/* With server callbacks installed, libssh dispatches unhandled requests
 * here and never places them on the synchronous ssh_message_get queue. */
static int channel_request(ssh_session session, ssh_message msg, void *userdata)
{
    struct SessionCtx *ctx = userdata;
    (void)session;
    if (!ctx->authenticated)
        return 1;
    if (ssh_message_type(msg) == SSH_REQUEST_CHANNEL_OPEN && !ctx->channel &&
        ssh_message_subtype(msg) == SSH_CHANNEL_SESSION)
    {
        ctx->channel = ssh_message_channel_request_open_reply_accept(msg);
        return 0;
    }
    if (!ctx->channel || ssh_message_type(msg) != SSH_REQUEST_CHANNEL ||
        ssh_message_channel_request_channel(msg) != ctx->channel)
        return 1;
    switch (ssh_message_subtype(msg))
    {
        case SSH_CHANNEL_REQUEST_WINDOW_CHANGE:
            break;
        case SSH_CHANNEL_REQUEST_PTY:
            if (ctx->started) return 1;
            break;
        case SSH_CHANNEL_REQUEST_EXEC:
        {
            const char *command = ssh_message_channel_request_command(msg);
            if (ctx->started || !command || !(ctx->command = strdup(command))) return 1;
            ctx->started = TRUE;
            break;
        }
        case SSH_CHANNEL_REQUEST_SHELL:
            if (ctx->started) return 1;
            ctx->started = TRUE;
            break;
        default:
            return 1;
    }
    ssh_message_channel_request_reply_success(msg);
    return 0;
}

static void execute_request(ssh_channel channel, const char *command)
{
    struct CommandExit exit = {0};
    char name[64], buf[BUF_SIZE];
    LONG size, rc = RETURN_FAIL;
    make_pipe_name(name, sizeof(name), "exec");
    BPTR writer = Open(name, MODE_NEWFILE);
    BPTR reader = writer ? Open(name, MODE_OLDFILE) : BNULL;
    BPTR input = Open("NIL:", MODE_OLDFILE);
    exit.message.mn_ReplyPort = CreateMsgPort();
    if (reader && input && exit.message.mn_ReplyPort &&
        SystemTags(command, SYS_Asynch, TRUE, SYS_Input, (IPTR)input,
            SYS_Output, (IPTR)writer, NP_ExitCode, (IPTR)command_exited,
            NP_ExitData, (IPTR)&exit, TAG_DONE) != -1)
    {
        /* The asynchronous shell owns input and writer. Drain as it runs
         * so large output can never fill the pipe and block the child. */
        input = writer = BNULL;
        BOOL connected = TRUE;
        while ((size = Read(reader, buf, sizeof(buf))) > 0)
        {
            for (LONG pos = 0; connected && pos < size;)
            {
                int n = ssh_channel_write(channel, buf + pos, size - pos);
                if (n <= 0) connected = FALSE;
                else pos += n;
            }
        }
        WaitPort(exit.message.mn_ReplyPort);
        GetMsg(exit.message.mn_ReplyPort);
        if (size == 0) rc = exit.status;
    }
    if (writer) Close(writer);
    if (reader) Close(reader);
    if (input) Close(input);
    if (exit.message.mn_ReplyPort) DeleteMsgPort(exit.message.mn_ReplyPort);
    ssh_channel_request_send_exit_status(channel, rc);
}

/* --- Per-connection session handler --- */
static void handle_session(ssh_session session)
{
    struct ssh_server_callbacks_struct cb = {0};
    struct SessionCtx ctx = {0};
    ssh_channel channel = NULL;

    cb.auth_pubkey_function = auth_pubkey_cb;
    cb.auth_password_function = auth_password_cb;
    cb.userdata = &ctx;

    ssh_callbacks_init(&cb);
    ssh_set_server_callbacks(session, &cb);
    ssh_set_message_callback(session, channel_request, &ctx);
    ssh_set_auth_methods(session, SSH_AUTH_METHOD_PUBLICKEY | SSH_AUTH_METHOD_PASSWORD);

    if (ssh_handle_key_exchange(session) != SSH_OK)
    {
        Printf("sshd: key exchange failed: %s\n", (IPTR)ssh_get_error(session));
        goto done;
    }

    /* Authentication loop */
    ssh_event event = ssh_event_new();
    if (!event) goto done;
    if (ssh_event_add_session(event, session) != SSH_OK) { ssh_event_free(event); goto done; }

    int n_auth = 0;
    while (!ctx.authenticated && n_auth < 20)
    {
        if (ssh_event_dopoll(event, 1000) == SSH_ERROR)
            break;
        n_auth++;
    }

    for (int i = 0; ctx.authenticated && !ctx.started && i < 30; ++i)
        if (!ssh_is_connected(session) || ssh_event_dopoll(event, 1000) == SSH_ERROR) break;

    ssh_event_free(event);
    channel = ctx.channel;
    if (!ctx.started) goto cleanup_channel;

    if (ctx.command)
    {
        execute_request(channel, ctx.command);
        free(ctx.command);
    }
    else
    {
        /* Interactive shell with PTY bridge */
        handle_shell(channel);
    }

cleanup_channel:
    if (channel)
    {
        ssh_channel_send_eof(channel);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
    }

done:
    ssh_disconnect(session);
    ssh_free(session);
}

/* Serve an accepted socket owned by the calling task's base. */
static void serve_socket(ssh_bind sshbind, LONG fd)
{
    ssh_session session = ssh_new();

    if (session == NULL)
    {
        CloseSocket(fd);
        return;
    }

    if (ssh_bind_accept_fd(sshbind, session, fd) != SSH_OK)
    {
        Printf("sshd: accept failed: %s\n", (IPTR)ssh_get_error(sshbind));
        CloseSocket(fd);
        ssh_free(session);
        return;
    }

    handle_session(session);
}

/* Load the session as a DOS program. A raw NP_Entry task has no C runtime
 * startup, including the per-task errno cell used by libssh. The new image
 * owns its library bases and host keys even if the listener is stopped. */
static void spawn_session(ssh_bind sshbind, LONG fd)
{
    char program[512], executable[512] = "PROGDIR:", args[48];
    BPTR seg = BNULL, input = BNULL;
    LONG socket_id;

    if (GetProgramDir() && GetProgramName(program, sizeof(program)) && program[0] &&
        AddPart(executable, FilePart(program), sizeof(executable)))
        seg = LoadSeg(executable);
    if (!seg)
        seg = LoadSeg("SYS:System/Network/Services/sshd");
    if (seg)
        input = Open("NIL:", MODE_OLDFILE);
    if (!input)
    {
        if (seg) UnLoadSeg(seg);
        serve_socket(sshbind, fd);
        return;
    }
    socket_id = ReleaseSocket(fd, UNIQUE_ID);
    if (socket_id >= 0)
    {
        snprintf(args, sizeof(args), "--session %ld\n", (long)socket_id);
        if (CreateNewProcTags(NP_Seglist, (IPTR)seg, NP_Cli, TRUE,
                NP_Name, (IPTR)"sshd_session", NP_Arguments, (IPTR)args,
                NP_Input, (IPTR)input, NP_CloseInput, TRUE,
                NP_StackSize, 131072, TAG_DONE))
            return;
        fd = ObtainSocket(socket_id, 0, 0, 0);
        if (fd >= 0) serve_socket(sshbind, fd);
    }
    else
        CloseSocket(fd);
    Close(input);
    UnLoadSeg(seg);
}

/* --- Main --- */
int main(int argc, char **argv)
{
    int port = SSHD_PORT;
    LONG session_id = -1, session_fd = -1;
    int i;

    /* Parse arguments */
    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--session") == 0 && i + 1 < argc)
        {
            char *end;
            long id = strtol(argv[++i], &end, 10);
            if (!*argv[i] || *end || id < 0 || id > 2147483647L)
                return RETURN_FAIL;
            session_id = id;
        }
        else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0)
        {
            Printf("AROS SSH Server 0.1 (libssh 0.12.0, mbedTLS 3.6.7)\n");
            return RETURN_OK;
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "?") == 0)
        {
            Printf("AROS SSH Server 0.1\n");
            Printf("Usage: sshd [-p port] [-V]\n");
            return RETURN_OK;
        }
    }

    DaemonSocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!DaemonSocketBase)
    {
        Printf("sshd: Cannot open bsdsocket.library v4\n");
        return RETURN_FAIL;
    }

    if (session_id >= 0)
    {
        session_fd = ObtainSocket(session_id, 0, 0, 0);
        if (session_fd < 0)
        {
            CloseLibrary(DaemonSocketBase);
            return RETURN_FAIL;
        }
    }
    if (session_fd < 0 && !ensure_host_key())
    {
        CloseLibrary(DaemonSocketBase);
        return RETURN_FAIL;
    }

    ssh_init();

    ssh_bind sshbind = ssh_bind_new();
    if (!sshbind)
    {
        if (session_fd >= 0) CloseSocket(session_fd);
        Printf("sshd: Cannot create SSH bind\n");
        ssh_finalize();
        CloseLibrary(DaemonSocketBase);
        return RETURN_FAIL;
    }

    char port_str[8];
    sprintf(port_str, "%d", port);

    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, port_str);
    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, SSHD_HOST_KEY);
    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BANNER, SSHD_BANNER);

    if (session_fd >= 0)
    {
        serve_socket(sshbind, session_fd);
        ssh_bind_free(sshbind);
        ssh_finalize();
        CloseLibrary(DaemonSocketBase);
        return RETURN_OK;
    }

    if (ssh_bind_listen(sshbind) < 0)
    {
        Printf("sshd: Listen failed: %s\n", (IPTR)ssh_get_error(sshbind));
        ssh_bind_free(sshbind);
        ssh_finalize();
        CloseLibrary(DaemonSocketBase);
        return RETURN_FAIL;
    }

    Printf("sshd: " SSHD_BANNER " listening on port %ld\n", (long)port);

    /*
     * Register with netservices.library when it is present, so the stack's
     * service manager can run us from db/services.d/sshd and drive us across a
     * configuration reload and a clean shutdown.  Started from a plain CLI
     * (no stack service manager) this simply doesn't register and CTRL-C still
     * works.  We ask for the reconfigure-begin/end and stop signals; on begin
     * we only pause accepting (we must NOT call into the stack while it holds
     * its reload fence), on end we resume, on stop we exit.
     */
    BYTE  sStop  = AllocSignal(-1);
    BYTE  sBegin = AllocSignal(-1);
    BYTE  sEnd   = AllocSignal(-1);
    ULONG mStop  = (sStop  >= 0) ? (1UL << sStop)  : 0;
    ULONG mBegin = (sBegin >= 0) ? (1UL << sBegin) : 0;
    ULONG mEnd   = (sEnd   >= 0) ? (1UL << sEnd)   : 0;
    APTR  nshandle = NULL;
    BOOL  paused = FALSE;

    NetServicesBase = OpenLibrary(NETSERVICESNAME, 0);
    if (NetServicesBase)
    {
        struct TagItem nstags[] = {
            { NETSERVICE_Name,             (IPTR)"sshd" },
            { NETSERVICE_Order,            (IPTR)NSPRI_RCDAEMONS },
            { NETSERVICE_ReconfigBeginSig, (IPTR)sBegin },
            { NETSERVICE_ReconfigEndSig,   (IPTR)sEnd },
            { NETSERVICE_StopSig,          (IPTR)sStop },
            { TAG_DONE, 0 }
        };
        nshandle = RegisterNetService(nstags);
    }
    if (nshandle)
        Printf("sshd: registered as a network service\n");
    else
        Printf("sshd: Press CTRL-C to shutdown\n");

    int bind_fd = ssh_bind_get_fd(sshbind);
    size_t nfwords = (size_t)bind_fd / NFDBITS + 1;
    fd_mask *read_words = calloc(nfwords, sizeof(*read_words));
    int listener_rc = RETURN_OK;
    if (!read_words)
    {
        Printf("sshd: Cannot allocate the listener descriptor set\n");
        listener_rc = RETURN_FAIL;
    }

    /* Accept loop.  Existing sessions run in their own processes, unaffected. */
    while (read_words && !g_shutdown)
    {
        ULONG sigs;

        if (paused)
        {
            /* Quiesced during a stack reconfigure: wait only on signals. */
            sigs = Wait(mStop | mBegin | mEnd | SIGBREAKF_CTRL_C);
        }
        else
        {
            struct timeval tv;
            ULONG sigmask = mStop | mBegin | mEnd | SIGBREAKF_CTRL_C;
            int sel;

            memset(read_words, 0, nfwords * sizeof(*read_words));
            read_words[bind_fd / NFDBITS] = 1U << (bind_fd % NFDBITS);
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            /* WaitSelect also returns when one of the requested signals fires;
             * sigmask comes back holding the signals that were received. */
            sel = WaitSelect(bind_fd + 1, (fd_set *)read_words, NULL, NULL, &tv, &sigmask);
            if (sel < 0)
            {
                if (Errno() != EINTR)
                {
                    Printf("sshd: WaitSelect failed (%ld)\n", (IPTR)Errno());
                    listener_rc = RETURN_FAIL;
                    break;
                }
                sigs = SetSignal(0, sigmask) & sigmask;
            }
            else sigs = sigmask;

            if (sel > 0)
            {
                LONG fd = accept(bind_fd, NULL, NULL);

                if (fd >= 0)
                    spawn_session(sshbind, fd);
            }
        }

        if (sigs & (mStop | SIGBREAKF_CTRL_C))
        {
            Printf("sshd: Shutdown requested\n");
            break;
        }
        if (sigs & mBegin)
            paused = TRUE;      /* reconfigure beginning - stop touching the stack */
        if (sigs & mEnd)
            paused = FALSE;     /* reconfigure done - resume accepting */
    }

    free(read_words);
    if (nshandle)
        UnregisterNetService(nshandle);
    if (NetServicesBase)
        CloseLibrary(NetServicesBase);
    if (sStop  >= 0) FreeSignal(sStop);
    if (sBegin >= 0) FreeSignal(sBegin);
    if (sEnd   >= 0) FreeSignal(sEnd);

    ssh_bind_free(sshbind);
    ssh_finalize();
    CloseLibrary(DaemonSocketBase);

    Printf("sshd: Stopped\n");
    return listener_rc;
}
