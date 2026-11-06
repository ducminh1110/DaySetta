#!/usr/bin/env python3
"""Run macOS 26.6 BaseSystem daemons (the user's own copy) under Darling's launchd.

For each daemon: write a launchd job into the prefix (~/.darling/System/Library/LaunchDaemons) that keeps Apple's
label and MachServices, runs the BaseSystem binary in place (mldr picks Darling's dyld port for it through
DARLING_DYLINKER / DARLING_DYLINKER_PATHS, so app bundles such as RecoveryOS Agent.app keep their bundle) and adds
the shared-cache environment. Run with the container stopped.
  mkdaemons.py [--undo] [--only=notifyd,cfprefsd]
"""
import os, plistlib, subprocess, sys

HOME = os.path.expanduser('~')
BS = HOME + '/basesystem'
OUT = HOME + '/darltest/daemons'
JOBS = HOME + '/.darling/System/Library/LaunchDaemons'  # the only directory Darling's launchctl bootstrap loads
DAEMONS = [  # (plist in BaseSystem, program)
    ('System/Library/LaunchDaemons/com.apple.notifyd.plist', 'usr/sbin/notifyd'),
    ('System/Library/LaunchDaemons/com.apple.cfprefsd.xpc.daemon.plist', 'usr/sbin/cfprefsd'),
    ('System/Library/LaunchAgents/com.apple.fontd.useragent.plist',
     'System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks/ATS.framework/Versions/A/Support/fontd'),
    ('System/Library/LaunchDaemons/com.apple.bsd.dirhelper.plist', 'usr/libexec/dirhelper'),
    ('System/Library/LaunchAgents/com.apple.FontWorker.plist',
     'System/Library/Frameworks/ApplicationServices.framework/Versions/A/Frameworks/ATS.framework/Versions/A/Support/fontworker'),
    ('System/Library/LaunchDaemons/com.apple.coreservices.launchservicesd.plist', 'System/Library/CoreServices/launchservicesd'),
    # System XPC services of frameworks (macOS launchd creates these jobs from the bundles; Darling's does not):
    # a job dict instead of a plist path.
    ({'Label': 'com.apple.authd', 'MachServices': {'com.apple.authd': True}},
     'System/Library/Frameworks/Security.framework/Versions/A/XPCServices/authd.xpc/Contents/MacOS/authd'),
    # recoveryOS services used by the CDIS apps
    ('System/Library/LaunchAgents/com.apple.recoveryos_agent.plist',
     'System/Library/CoreServices/RecoveryOS Agent.app/Contents/MacOS/RecoveryOS Agent'),
    ('System/Library/LaunchDaemons/com.apple.recoverydiagnosed.plist', 'usr/libexec/recoverydiagnosed'),
    ('System/Library/LaunchDaemons/com.apple.bosreporter.plist',
     'System/Library/PrivateFrameworks/BridgeOSInstallReporting.framework/Resources/bosreporter'),
]
ENV = {
    'DYLD_SHARED_CACHE_DIR': '/Volumes/SystemRoot' + BS + '/System/Library/dyld/',
    'DYLD_SHARED_REGION': 'private',
    'DARLING_DYLINKER': '/usr/lib/dyl4',
    'DARLING_DYLINKER_PATHS': BS,
    'DYLD_FALLBACK_LIBRARY_PATH': '/usr/lib/swift:/usr/local/lib:/usr/lib',
}

def job_file_name(plist):
    return plist['Label'] + '.plist' if isinstance(plist, dict) else os.path.basename(plist)

if '--undo' in sys.argv:
    for plist, _ in DAEMONS:
        p = os.path.join(JOBS, 'org.darling.macos26.' + job_file_name(plist))
        if os.path.exists(p):
            os.unlink(p)
    sys.exit(0)

only = [a.split('=', 1)[1] for a in sys.argv if a.startswith('--only=')]
if only:
    DAEMONS = [d for d in DAEMONS if os.path.basename(d[1]) in only[0].split(',')]

os.makedirs(OUT, exist_ok=True)
os.makedirs(JOBS, exist_ok=True)
for plist, program in DAEMONS:
    job = dict(plist) if isinstance(plist, dict) else plistlib.load(open(os.path.join(BS, plist), 'rb'))
    if isinstance(plist, dict):
        # XPC service bundles get XPC_SERVICE_NAME from launchd on macOS
        job['EnvironmentVariables'] = {'XPC_SERVICE_NAME': plist['Label']}
    args = list(job.get('ProgramArguments', [program]))
    args[0] = '/Volumes/SystemRoot' + os.path.join(BS, program)
    job['ProgramArguments'] = args
    job.pop('Program', None)
    job['EnvironmentVariables'] = dict(job.get('EnvironmentVariables', {}), **ENV, OS_ACTIVITY_DT_MODE='YES')
    # os_log goes to stderr (no logd here): keep it per job
    job['StandardErrorPath'] = '/private/tmp/%s.log' % job['Label']
    job['StandardOutPath'] = '/private/tmp/%s.log' % job['Label']
    for key in ('LimitLoadToSessionType', 'POSIXSpawnType', 'PublishesEvents', 'EnablePressuredExit',
                'StartCalendarInterval', 'RunAtLoad'):
        job.pop(key, None)  # Darling's launchd has one session and no event publishers
    out = os.path.join(JOBS, 'org.darling.macos26.' + job_file_name(plist))
    plistlib.dump(job, open(out, 'wb'))
    os.chmod(out, 0o644)  # launchctl skips job files that group/others can write
    print('job', out, '->', args[0])
