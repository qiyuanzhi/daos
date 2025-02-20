#!/bin/bash

set -eux

: "${DAOS_STACK_RETRY_DELAY_SECONDS:=60}"
: "${DAOS_STACK_RETRY_COUNT:=3}"
: "${DAOS_STACK_MONITOR_SECONDS:=600}"
: "${BUILD_URL:=Not_in_jenkins}"
: "${STAGE_NAME:=Unknown_Stage}"
: "${OPERATIONS_EMAIL:=$USER@localhost}"

retry_dnf() {
    local monitor_threshold="$1"
    shift

    local args=("dnf" "-y" "${@}")
    local attempt=0
    local rc=0
    while [ $attempt -lt "${RETRY_COUNT:-$DAOS_STACK_RETRY_COUNT}" ]; do
        if monitor_cmd "$monitor_threshold" "${args[@]}"; then
            # Command succeeded, return with success
            if [ $attempt -gt 0 ]; then
                # shellcheck disable=SC2154
                send_mail "Command retry successful in $STAGE_NAME after $((attempt + 1)) attempts using ${repo_servers[0]} as initial repo server " \
                          "Command:  ${args[*]}\nAttempts: $attempt\nStatus:   $rc"
            fi
            return 0
        fi
        # Command failed, retry
        rc=${PIPESTATUS[0]}
        (( attempt++ )) || true
        if [ "$attempt" -gt 0 ]; then
            # shellcheck disable=SC2154
            if [ "$attempt" -eq 2 ] && [ ${#repo_servers[@]} -gt 1 ]; then
                # but we were using an experimental repo server, so fall back to the
                # non-experimental one after trying twice with the experimental one
                set_local_repo "${repo_servers[1]}"
                dnf -y makecache
                if [ -n "${POWERTOOLSREPO:-}" ]; then
                    POWERTOOLSREPO=${POWERTOOLSREPO/${repo_servers[0]}/${repo_servers[1]}}
                fi
            fi
            sleep "${RETRY_DELAY_SECONDS:-$DAOS_STACK_RETRY_DELAY_SECONDS}"
        fi
    done
    if [ "$rc" -ne 0 ]; then
        send_mail "Command retry failed in $STAGE_NAME after $attempt attempts using ${repo_server:-nexus} as initial repo server " \
                  "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
    fi
    return 1

}

send_mail() {
    local subject="$1"
    local message="${2:-}"

    set +x
    {
        echo "Build: $BUILD_URL"
        echo "Stage: $STAGE_NAME"
        echo "Host:  $HOSTNAME"
        echo ""
        echo -e "$message"
    } 2>&1 | mail -s "$subject" -r "$HOSTNAME"@intel.com "$OPERATIONS_EMAIL"
    set -x
}

monitor_cmd() {
    local threshold="$1"
    shift

    local duration=0
    local start="$SECONDS"
    if ! time "$@"; then
        return "${PIPESTATUS[0]}"
    fi
    ((duration = SECONDS - start))
    if [ "$duration" -gt "$threshold" ]; then
        send_mail "Command exceeded ${threshold}s in $STAGE_NAME" \
                    "Command:  $*\nReal time: $duration"
    fi
    return 0
}

retry_cmd() {
    local monitor_threshold="$1"
    shift

    local attempt=0
    local rc=0
    while [ $attempt -lt "${RETRY_COUNT:-$DAOS_STACK_RETRY_COUNT}" ]; do
        if monitor_cmd "$monitor_threshold" "$@"; then
            # Command succeeded, return with success
            if [ $attempt -gt 0 ]; then
                send_mail "Command retry successful in $STAGE_NAME after $attempt attempts" \
                          "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
            fi
            return 0
        fi
        # Command failed, retry
        rc=${PIPESTATUS[0]}
        (( attempt++ )) || true
        if [ "$attempt" -gt 0 ]; then
            sleep "${RETRY_DELAY_SECONDS:-$DAOS_STACK_RETRY_DELAY_SECONDS}"
        fi
    done
    if [ "$rc" -ne 0 ]; then
        send_mail "Command retry failed in $STAGE_NAME after $attempt attempts" \
                  "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
    fi
    return 1
}

timeout_cmd() {
    local timeout="$1"
    shift

    local attempt=0
    local rc=1
    while [ $attempt -lt "${RETRY_COUNT:-$DAOS_STACK_RETRY_COUNT}" ]; do
        if monitor_cmd "$DAOS_STACK_MONITOR_SECONDS" timeout "$timeout" "$@"; then
            # Command succeeded, return with success
            if [ $attempt -gt 0 ]; then
                send_mail "Command timeout successful in $STAGE_NAME after $attempt attempts" \
                          "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
            fi
            return 0
        fi
        rc=${PIPESTATUS[0]}
        if [ "$rc" = "124" ]; then
            # Command timed out, try again
            (( attempt++ )) || true
            continue
        fi
        # Command failed for something other than timeout
        break
    done
    if [ "$rc" -ne 0 ]; then
        send_mail "Command timeout failed in $STAGE_NAME after $attempt attempts" \
                  "Command:  $*\nAttempts: $attempt\nStatus:   $rc"
    fi
    return "$rc"
}

fetch_repo_config() {
    local repo_server="$1"

    local repo_file="daos_ci-${DISTRO_NAME}-$repo_server"
    local repopath="${REPOS_DIR}/$repo_file"
    if ! curl -f -o "$repopath" "$REPO_FILE_URL$repo_file.repo"; then
        return 1
    fi

    # ugly hackery for nexus repo naming
    if [ "$repo_server" = "nexus" ]; then
        local version
        version="$(lsb_release -sr)"
        version=${version%.*}
        sed -i -e "s/\$releasever/$version/g" "$repopath"
    fi

    return 0
}

set_local_repo() {
    local repo_server="$1"

    rm -f "$REPOS_DIR"/daos_ci-"$DISTRO_NAME".repo
    ln "$REPOS_DIR"/daos_ci-"$DISTRO_NAME"{-"$repo_server",.repo}

    local version
    version="$(lsb_release -sr)"
    version=${version%%.*}
    if [ "$repo_server" = "artifactory" ] &&
        [[ $(echo "$COMMIT_MESSAGE" | sed -ne '/^PR-repos: */s/^[^:]*: *//p') = *daos@* ]] ||
        [[ $(echo "$COMMIT_MESSAGE" |
             sed -ne "/^PR-repos-$DISTRO: */s/^[^:]*: *//p") = *daos@* ]] ||
        [ -z "$(echo "$COMMIT_MESSAGE" | sed -ne '/^RPM-test-version: */s/^[^:]*: *//p')" ]; then
        # Disable the daos repo so that the Jenkins job repo is used for daos packages
        dnf -y config-manager \
            --disable daos-stack-daos-"${DISTRO_GENERIC}"-"$version"-x86_64-stable-local-artifactory
    fi
}

update_repos() {
    local DISTRO_NAME="$1"

    # Update the repo files
    local repo_server
    for repo_server in "${repo_servers[@]}"; do
        if ! fetch_repo_config "$repo_server"; then
            # leave the existing on-image repo config alone if the repo fetch fails
            send_mail "Fetch repo file for repo server \"$repo_server\" failed.  Continuing on with in-image repos."
            return 1
        fi
    done

    # we're not actually using the set_local_repos.sh script
    # setting a repo server is as easy as renaming a file
    #if ! curl -o /usr/local/sbin/set_local_repos.sh-tmp "${REPO_FILE_URL}set_local_repos.sh"; then
    #    send_mail "Fetch set_local_repos.sh failed.  Continuing on with in-image copy."
    #else
    #    cat /usr/local/sbin/set_local_repos.sh-tmp > /usr/local/sbin/set_local_repos.sh
    #    chmod +x /usr/local/sbin/set_local_repos.sh
    #    rm -f /usr/local/sbin/set_local_repos.sh-tmp
    #fi

    # successfully grabbed them all, so replace the entire $REPOS_DIR
    # content with them
    local file
    for file in "$REPOS_DIR"/*.repo; do
        [ -e "$file" ] || break
        # empty the file but keep it around so that updates don't recreate it
        true > "$file"
    done

    set_local_repo "${repo_servers[0]}"
}

post_provision_config_nodes() {
    bootstrap_dnf

    # Reserve port ranges 31416-31516 for DAOS and CART servers
    echo 31416-31516 > /proc/sys/net/ipv4/ip_local_reserved_ports

    if $CONFIG_POWER_ONLY; then
        rm -f "$REPOS_DIR"/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
        time dnf -y erase fio fuse ior-hpc mpich-autoload               \
                     ompi argobots cart daos daos-client dpdk      \
                     fuse-libs libisa-l libpmemobj mercury mpich   \
                     openpa pmix protobuf-c spdk libfabric libpmem \
                     libpmemblk munge-libs munge slurm             \
                     slurm-example-configs slurmctld slurm-slurmmd
    fi

    # shellcheck disable=SC2154
    if ! update_repos "$DISTRO_NAME"; then
        # need to use the image supplied repos
        # shellcheck disable=SC2034
        repo_servers=()
    fi

    if [ -n "$INST_REPOS" ]; then
        local repo
        for repo in $INST_REPOS; do
            branch="master"
            build_number="lastSuccessfulBuild"
            if [[ $repo = *@* ]]; then
                branch="${repo#*@}"
                repo="${repo%@*}"
                if [[ $branch = *:* ]]; then
                    build_number="${branch#*:}"
                    branch="${branch%:*}"
                fi
            fi
            local repo_url="${JENKINS_URL}"job/daos-stack/job/"${repo}"/job/"${branch//\//%252F}"/"${build_number}"/artifact/artifacts/$DISTRO_NAME/
            dnf -y config-manager --add-repo="${repo_url}"
            disable_gpg_check "$repo_url"
        done
    fi
    if [ -n "$INST_RPMS" ]; then
        # shellcheck disable=SC2086
        time dnf -y erase $INST_RPMS
    fi
    rm -f /etc/profile.d/openmpi.sh
    rm -f /tmp/daos_control.log
    if [ -n "${LSB_RELEASE:-}" ]; then
        if ! rpm -q "$LSB_RELEASE"; then
            RETRY_COUNT=4 retry_dnf 360 install "$LSB_RELEASE"
        fi
    fi

    # shellcheck disable=SC2001
    if ! rpm -q "$(echo "$INST_RPMS" |
                   sed -e 's/--exclude [^ ]*//'                 \
                       -e 's/[^ ]*-daos-[0-9][0-9]*//g')"; then
        # shellcheck disable=SC2086
        if [ -n "$INST_RPMS" ]; then
            # shellcheck disable=SC2154
            if ! RETRY_COUNT=4 retry_dnf 360 install $INST_RPMS; then
                rc=${PIPESTATUS[0]}
                dump_repos
                exit "$rc"
            fi
        fi
    fi

    distro_custom

    lsb_release -a

    # now make sure everything is fully up-to-date
    # shellcheck disable=SC2154
    if ! RETRY_COUNT=4 retry_dnf 600 upgrade --exclude "$EXCLUDE_UPGRADE"; then
        dump_repos
        exit 1
    fi

    lsb_release -a

    if [ -f /etc/do-release ]; then
        cat /etc/do-release
    fi
    cat /etc/os-release

    exit 0
}
