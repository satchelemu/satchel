"""Session lifetime for the pinned engine's cloud save sync (dm/host/save_sync.c, nxnative, MIT).

Upstream starts one detached sender thread per process and mounts once per process, because
one app launch plays one game. Satchel plays several sessions in one process, so the sender
must be flushed and joined when a session stops, and the per-process state must start clean
for the next one. Exact replacements fail on upstream drift.
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent


def replace(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)


def save_sync(text):
    text = replace(text, 'static char g_save[PATH_MAX], g_title[24], g_cloud[PATH_MAX];\n',
                   'static char g_save[PATH_MAX], g_title[24], g_cloud[PATH_MAX];\n'
                   'static int satchel_sync_stop, satchel_sync_exited;   // the session is stopping; the sender has left\n'
                   'static pthread_t satchel_sync_thread;\n')
    # The sender waits for a Commit, a flush or the session's stop, and leaves under its lock.
    text = replace(text, '        while (g_commits == g_sent && !g_want) pthread_cond_wait(&g_cv, &g_mu);\n',
                   '        while (g_commits == g_sent && !g_want && !satchel_sync_stop) pthread_cond_wait(&g_cv, &g_mu);\n'
                   '        if (satchel_sync_stop) break;\n')
    text = replace(text, '            while (!g_want && g_commits == seen && r != ETIMEDOUT) r = pthread_cond_timedwait(&g_cv, &g_mu, &ts);\n',
                   '            while (!g_want && !satchel_sync_stop && g_commits == seen && r != ETIMEDOUT) r = pthread_cond_timedwait(&g_cv, &g_mu, &ts);\n')
    text = replace(text, '''        g_busy = 0; g_sent = upto; g_failed = !ok;
        pthread_cond_broadcast(&g_cv);
    }
    return NULL;
}''', '''        g_busy = 0; g_sent = upto; g_failed = !ok;
        pthread_cond_broadcast(&g_cv);
    }
    satchel_sync_exited = 1; pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    return NULL;
}''')
    # Joinable, so the session's stop can wait for an in-flight send before files are reset.
    text = replace(text, '''    pthread_t th;
    if (pthread_create(&th, NULL, sender, NULL)) { SRED("cannot start the sender: saves are not sent this session"); return; }
    pthread_detach(th);
    atomic_store(&g_on, 1);''', '''    satchel_sync_stop = satchel_sync_exited = 0;
    if (pthread_create(&satchel_sync_thread, NULL, sender, NULL)) { SRED("cannot start the sender: saves are not sent this session"); return; }
    atomic_store(&g_on, 1);''')
    return text + '\n' + (HERE / 'SessionSaveSync.inc').read_text()


def prepare(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(save_sync(source.read_text()))
    return destination
