#ifndef M4VH263DEC_COMPAT_LOG_H
#define M4VH263DEC_COMPAT_LOG_H

// Stands in for Android's <log/log.h> so the vendored decoder builds unmodified off-Android.
//
// ALOGE is the only macro the decoder uses, and only on paths it has already decided are
// unrecoverable -- it returns a failure immediately afterwards. Dropping the message loses a log
// line and nothing else; the caller still sees the failure.
#ifndef ALOGE
#define ALOGE(...) ((void)0)
#endif

// The security-report hook. On Android this files a tagged event; off it, the decoder's own
// failure return is the whole story, so it does nothing. Returns 0 the way the real one does on
// success, since a couple of call sites read the result.
#ifndef android_errorWriteLog
#define android_errorWriteLog(tag, subTag) (0)
#endif

#endif
