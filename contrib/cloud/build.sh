#!/usr/bin/env bash

DOCKER_REPOSITORY="cubrid/cubrid"
GIT_FORK=""
GIT_REVISION=""
DOCKER_BUILD_ARGS=""
DOCKER_IMAGE=""
INSTALL_SOURCES="OFF"

build_docker_image ()
{
    DOCKER_IMAGE="$DOCKER_REPOSITORY:$GIT_FORK-$GIT_REVISION"
    echo "docker: building image: $DOCKER_IMAGE"

    if [[ ! -z "$DOCKER_BUILD_ARGS" ]]; then
        echo "        with arguments: $DOCKER_BUILD_ARGS"
    else
        echo "        with default arguments"
    fi

    docker build --no-cache ${DOCKER_BUILD_ARGS} --tag ${DOCKER_IMAGE} . &> /dev/null
}

kubernetes_apply ()
{
    echo "kubernetes: applying cubrid configuration files using image $DOCKER_IMAGE"

    sed -i -E "s/image:.*/image: ${DOCKER_IMAGE//\//\\/}/g" cubrid-statefulset.yaml
    kubectl apply -f cubrid-statefulset.yaml
}

kubernetes_delete ()
{
    echo "kubernetes: deleting cubrid resources"
    kubectl delete statefulset cubrid &> /dev/null
    kubectl delete service -l app=cubrid &> /dev/null

    echo "kubernetes: waiting for pods to terminate"
    while [[ $(kubectl get pods | grep cubrid | wc -l) -gt 0 ]]; do
      sleep 0.5
    done

    kubectl delete pvc -l app=cubrid &> /dev/null
}

function show_usage ()
{
  echo "usage: $0 [options] [target]"
  echo ""
  echo "target"
  echo "  build-docker-image"
  echo "options"
  echo "  -t=[STRING], --build-type=[STRING]    build type, possible values: Debug, RelWithDebInfo and Release [default: Debug]"
  echo "  -j=[NUMBER], --build-jobs=[NUMBER]    number of parallel jobs for make [default: 4]"
  echo "  -f=[STRING], --git-fork=[STRING]      git fork use for build [default: CUBRID]"
  echo "                                        e.g. https://github.com/CUBRID/cubrid.git"
  echo "                                                                ~~~~~~"
  echo "  -r=[STRING], --git-revision=[STRING]  git revision to checkout [default: develop]"
  echo "                                        value can be a <branch name> or <commit>"
  echo "  -s, --install-sources                 install sources on make install [default is disabled]"
  echo ""
  echo "target"
  echo "  kubernetes-apply"
  echo "options"
  echo "  -i, --image                           docker image id or name"
  echo ""
  echo "target"
  echo "  kubernetes-delete"
  echo ""
  echo "global options"
  echo "  -h, --help                            print this message"

}

for ARG in "$@"; do
    case $ARG in
        -t=*|--build-type=*)
            DOCKER_BUILD_ARGS="--build-arg BUILD_TYPE=${ARG#*=} $DOCKER_BUILD_ARGS"
            shift
            ;;
        -j=*|--build-jobs=*)
            DOCKER_BUILD_ARGS="--build-arg MAKEFLAGS=-j${ARG#*=} $DOCKER_BUILD_ARGS"
            shift
            ;;
        -f=*|--git-fork=*)
            GIT_FORK=${ARG#*=}
            DOCKER_BUILD_ARGS="--build-arg GIT_FORK=$GIT_FORK $DOCKER_BUILD_ARGS"
            shift
            ;;
        -r=*|--git-revision=*)
            GIT_REVISION=${ARG#*=}
            DOCKER_BUILD_ARGS="--build-arg GIT_REVISION=$GIT_REVISION $DOCKER_BUILD_ARGS"
            shift
            ;;
        -s|--install-sources)
            INSTALL_SOURCES="ON"
            DOCKER_BUILD_ARGS="--build-arg INSTALL_SOURCES=$INSTALL_SOURCES $DOCKER_BUILD_ARGS"
            shift
            ;;
        -i=*|--image=*)
            DOCKER_IMAGE=${ARG#*=}
            shift
            ;;
        build-docker-image)
            build_docker_image
            shift
            ;;
        kubernetes-apply)
            kubernetes_apply
            shift
            ;;
        kubernetes-delete)
            kubernetes_delete
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            echo "unknown argument $ARG"
            show_usage
            exit 1
            ;;
    esac
done
