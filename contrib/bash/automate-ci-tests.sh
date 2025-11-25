#!/bin/bash

# written by hina@cubrid.com

#
# Directory structure around this script:
#
# /home/cubrid/automate-ci-tests/
#   + automate-ci-tests.sh     <-- this script
#   + cubrid/
#   + cubrid-testcases/
#   + cubrid-testcases-private-ex/
#
# Summary:
# If new commits are detected in cubrid develop branch, then push each commit into shell-test branch of
# myfork repository and issue '/run all' comment in some pull request for the shell-test branch.
# If no new commits in cubrid develop branch, then check develop branches of cubrid-testcases and
# cubrid-testcases-private-ex for a new commit and, if one found, issue an appropriate '/run ...' comment
# in the pull request.
#


set -e  # exit on error immediately
#set -x  # echo commands

GITHUB_TOKEN=''     # use your own token
PR_NUM=''           # use your own PR

function issue_run_comment() {

    local targets="$1"

    echo "# Issuing '/run $targets'"

    curl -L \
         -X POST \
         -H "Accept: application/vnd.github+json" \
         -H "Authorization: Bearer $GITHUB_TOKEN" \
         -H "X-GitHub-Api-Version: 2022-11-28" \
         https://api.github.com/repos/CUBRID/cubrid/issues/$PR_NUM/comments \
         -d "{\"body\": \"/run $targets\"}" > /dev/null
}

function get_pr_head_sha() {

    echo $(curl -s -H "Authorization: Bearer $GITHUB_TOKEN" \
                -H "Accept: application/vnd.github+json" \
                -H "X-GitHub-Api-Version: 2022-11-28" \
                https://api.github.com/repos/CUBRID/cubrid/pulls/$PR_NUM | jq -r '.head.sha')
}

function wait_until_prev_shell_test_done() {

    local pr_head_sha=$(get_pr_head_sha)

    local test_retried=no

    while true; do
        shell_test_state=$(curl -s -H "Authorization: Bearer $GITHUB_TOKEN" \
                          -H "Accept: application/vnd.github+json" \
                          -H "X-GitHub-Api-Version: 2022-11-28" \
                          https://api.github.com/repos/CUBRID/cubrid/commits/$pr_head_sha/status \
                     | jq -r '.statuses[] | select(.context=="ci/circleci: test_shell") | .state' 2> /dev/null \
                     | head -n1)
        if [ "$shell_test_state" = 'success' ] || [ "$shell_test_state" = 'failure' ]; then
            echo ''
            echo '# Now, no running previous Shell Test'
            break
        elif [ $test_retried = no ] && [ "$shell_test_state" = 'error' ]; then
            echo ''
            echo '# Previous shell test ended up with an error.'
            issue_run_comment 'all'
            test_retried=yes
        else
            build_state=$(curl -s -H "Authorization: Bearer $GITHUB_TOKEN" \
                              -H "Accept: application/vnd.github+json" \
                              -H "X-GitHub-Api-Version: 2022-11-28" \
                              https://api.github.com/repos/CUBRID/cubrid/commits/$pr_head_sha/status \
                         | jq -r '.statuses[] | select(.context=="ci/circleci: build") | .state' 2> /dev/null \
                         | head -n1)
            if [ $test_retried = no ] && [ "$build_state" = 'failure' ]; then
                echo ''
                echo '# Previous test is incomplete due to a build failure.'
                issue_run_comment 'all'
                test_retried=yes
            else
                echo -n '.'
            fi
        fi

        sleep 31
    done
}

echo "# $(date)"

cd /home/cubrid/cron-jobs/automate-ci-tests/



cd cubrid
git fetch upstream
if [ -n "$(git log develop..upstream/develop)" ]; then

    while read -r line; do

        next_commit_sha="$(echo $line | cut -c 1-10)"
        echo "# Testing a new develop commit $next_commit_sha"

        wait_until_prev_shell_test_done

        git co shell-test

        msg="for commit $line"
        if [ "$next_commit_sha" = "${next_commit_sha% }" ]; then
            # simple format check of each line: character 10 must be a space
            echo "Error: unexpected commit hash '$next_commit_sha'"
            exit 1
        fi

        git merge $next_commit_sha --no-ff -q -m "$msg"
        git log -1 --oneline --no-decorate
        test_sha=$(git rev-parse HEAD)

        git push myfork
        sleep 11    # it seems that it takes some time for GitHub to apply the HEAD change
        new_pr_head_sha=$(get_pr_head_sha)
        if [ "$test_sha" != "$new_pr_head_sha" ]; then
            echo "Error: '$test_sha' is not the PR head yet ('$new_pr_head_sha' instead)"
            exit 1
        fi

        # issue '/run all' comment for the pushed new commit
        issue_run_comment 'all'

        git co develop
        git merge --ff-only $next_commit_sha

        echo "# Done with $next_commit_sha"

    done < <(git log --oneline --no-decorate --reverse develop..upstream/develop)

    echo "# Done with all new cubrid commits"

else
    echo "# No new commits of cubrid"
    no_new_commits=yes
fi

# check if some of the TCs have been updated

sql_tc_updated=no
shell_tc_updated=no

cd ../cubrid-testcases
git fetch upstream
if [ -n "$(git log develop..upstream/develop)" ]; then
    sql_tc_updated=yes
    git co develop
    git merge upstream/develop
fi

cd ../cubrid-testcases-private-ex
git fetch upstream
if [ -n "$(git log develop..upstream/develop)" ]; then
    shell_tc_updated=yes
    git co develop
    git merge upstream/develop
fi

if [ "$no_new_commits" = yes ]; then

    # No tests have been done in this case.
    # Execute a proper test if a TC has been updated

    targets=''

    if [ $sql_tc_updated = yes ] ; then
        if [ $shell_tc_updated = yes ]; then
            targets='all'
        else
            targets='sql medium'
        fi
    else
        if [ $shell_tc_updated = yes ]; then
            targets='shell'
        fi
    fi

    if [ -n "$targets" ]; then

        echo "# issuing '/run $targets' comments for updated TCs"

        wait_until_prev_shell_test_done
        issue_run_comment "$targets"
    fi
fi

