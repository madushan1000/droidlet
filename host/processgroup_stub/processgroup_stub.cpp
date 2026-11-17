/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Host replacement for libprocessgroup.  The host has no cgroups, so task
// profiles, cpusets and process groups are no-ops.  This keeps the
// android.os.Process JNI usable without building the real library (and its
// jsoncpp dependency).

#include <processgroup/processgroup.h>
#include <processgroup/processgroup_platform.h>
#include <processgroup/sched_policy.h>

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

SchedPolicy policyOrDefault(SchedPolicy policy) {
    return policy == SP_DEFAULT ? SP_SYSTEM_DEFAULT : policy;
}

}  // namespace

bool CgroupsAvailable() {
    return false;
}

bool CgroupGetControllerPath(const std::string&, std::string*) {
    return false;
}

bool CgroupGetControllerFromPath(const std::string&, std::string*) {
    return false;
}

bool CgroupGetAttributePath(const std::string&, std::string*) {
    return false;
}

bool CgroupGetAttributePathForTask(const std::string&, pid_t, std::string*) {
    return false;
}

bool CgroupGetAttributePathForProcess(std::string_view, uid_t, pid_t, std::string&) {
    return false;
}

bool SetTaskProfiles(pid_t, const std::vector<std::string>&, bool) {
    return true;
}

bool SetTaskProfiles(pid_t, std::initializer_list<std::string_view>, bool) {
    return true;
}

#if _LIBCPP_STD_VER > 17
bool SetTaskProfiles(pid_t, std::span<const std::string_view>, bool) {
    return true;
}
#endif

bool SetProcessProfiles(uid_t, pid_t, const std::vector<std::string>&) {
    return true;
}

bool SetProcessProfiles(uid_t, pid_t, std::initializer_list<std::string_view>) {
    return true;
}

#if _LIBCPP_STD_VER > 17
bool SetProcessProfiles(uid_t, pid_t, std::span<const std::string_view>) {
    return true;
}
#endif

bool SetUserProfiles(uid_t, const std::vector<std::string>&) {
    return true;
}

bool SetProcessProfilesCached(uid_t, pid_t, const std::vector<std::string>&) {
    return true;
}

bool UsePerAppMemcg() {
    return false;
}

void DropTaskProfilesResourceCaching() {}

int killProcessGroup(uid_t, pid_t, int) {
    return 0;
}

int killProcessGroupOnce(uid_t, pid_t, int) {
    return 0;
}

bool sendSignalToProcessGroup(uid_t, pid_t, int) {
    return false;
}

int createProcessGroup(uid_t, pid_t, bool) {
    return 0;
}

int createCGroupForCloneInto(uid_t, pid_t, uint64_t) {
    return 0;
}

bool setProcessGroupSwappiness(uid_t, pid_t, int) {
    return false;
}

bool setProcessGroupSoftLimit(uid_t, pid_t, int64_t) {
    return false;
}

bool setProcessGroupLimit(uid_t, pid_t, int64_t) {
    return false;
}

void removeAllEmptyProcessGroups() {}

bool isProfileValidForProcess(const std::string&, uid_t, pid_t) {
    return false;
}

namespace libprocessgroup_platform {

bool killProcessGroup(uid_t, pid_t) {
    return false;
}

}  // namespace libprocessgroup_platform

bool cpusets_enabled() {
    return false;
}

int set_cpuset_policy(int, SchedPolicy) {
    return 0;
}

int set_sched_policy(int, SchedPolicy) {
    return 0;
}

int get_sched_policy(int, SchedPolicy* policy) {
    *policy = SP_SYSTEM_DEFAULT;
    return 0;
}

const char* get_sched_policy_name(SchedPolicy policy) {
    policy = policyOrDefault(policy);
    static const char* const kSchedPolicyNames[] = {
            [SP_BACKGROUND] = "bg", [SP_FOREGROUND] = "fg", [SP_SYSTEM] = "  ",
            [SP_AUDIO_APP] = "aa",  [SP_AUDIO_SYS] = "as", [SP_TOP_APP] = "ta",
            [SP_RT_APP] = "rt",     [SP_RESTRICTED] = "rs", [SP_FOREGROUND_WINDOW] = "wi",
    };
    if (policy < SP_BACKGROUND || policy >= SP_CNT) {
        return nullptr;
    }
    return kSchedPolicyNames[policy];
}

const char* get_cpuset_policy_profile_name(SchedPolicy policy) {
    static constexpr const char* kCpusetProfiles[SP_CNT + 1] = {
            "CPUSET_SP_DEFAULT",      "CPUSET_SP_BACKGROUND", "CPUSET_SP_FOREGROUND",
            "CPUSET_SP_SYSTEM",       "CPUSET_SP_FOREGROUND", "CPUSET_SP_FOREGROUND",
            "CPUSET_SP_TOP_APP",      "CPUSET_SP_DEFAULT",    "CPUSET_SP_RESTRICTED",
            "CPUSET_SP_FOREGROUND_WINDOW"};
    if (policy < SP_DEFAULT || policy >= SP_CNT) {
        return nullptr;
    }
    return kCpusetProfiles[policy + 1];
}

const char* get_sched_policy_profile_name(SchedPolicy policy) {
    static constexpr const char* kSchedProfiles[SP_CNT + 1] = {
            "SCHED_SP_DEFAULT",      "SCHED_SP_BACKGROUND", "SCHED_SP_FOREGROUND",
            "SCHED_SP_SYSTEM",       "SCHED_SP_FOREGROUND", "SCHED_SP_FOREGROUND",
            "SCHED_SP_TOP_APP",      "SCHED_SP_RT_APP",     "SCHED_SP_DEFAULT",
            "SCHED_SP_FOREGROUND_WINDOW"};
    if (policy < SP_DEFAULT || policy >= SP_CNT) {
        return nullptr;
    }
    return kSchedProfiles[policy + 1];
}
