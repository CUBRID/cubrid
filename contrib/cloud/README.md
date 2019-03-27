# CUBRID on Kubernetes as a StatefulSet

Some tips to deploy CUBRID High-Availability cluster on Kubernetes

### Installation

* Install [kubectl]
* Install [minikube]
* Install [docker]

### Setup
After installation step, start minikube:
```bash
$ minikube start
```

To check for cluster info run:
```bash
$ minikube status
or
$ kubectl cluster-info
```

Minikube has a set of built in addons that can be used enabled, disabled, and opened inside of the local k8s environment.
To get a list of addons run:
```bash
$ minikube addons list
```

Enable dashboard and heapster then start dashboard:
```bash
$ minikube addons enable dashboard
$ minikube addons enable heapster
$ minikube dashboard
```

Minikube has a built-in Docker daemon. It is really handy when there is a need to run a local images in Kubernetes cluster
for more information check [use-local-images-by-re-using-the-docker-daemon]
In order to reuse the daemon run:
```bash
$ eval $(minikube docker-env)
```

### Deploying specific git <commit> or <branch>

#### Build docker image
In order to deploy a specific git <commit> or <branch> into a Kubernetes cluster first we need to build the git revision, for that run from this directory
```bash
$ ./build.sh --build-type=Debug --git-fork=CUBRID --git-revision=develop build-docker-image
$ ./build.sh --help # for more usage details
```
`--git-revision` can also be a git commit id, e.g. `--git-revision=bf7bf01`

This will generate a local docker images of CUBRID upstream, develop branch of debug build type, to check the images run:
```bash
$ docker images
REPOSITORY     TAG             IMAGE ID      CREATED      SIZE
cubrid/cubrid  CUBRID-develop  85329b913d22  3 hours ago  443MB
```

#### Deploy docker image into a Kubernetes cluster
After docker images is ready in order to deploy the image into Kubernetes cluster run:
```bash
$ ./build.sh --image=cubrid/cubrid:CUBRID-develop kubernetes-apply
```
Notice that `cubrid/cubrid:CUBRID-develop` is the image previously generated
For deleting all cubrid related resources from Kubernetes cluster run:
```bash
$ ./build.sh kubernetes-delete
```

Check for running pods and services using this command:
```bash
$ kubectl get all -l app=cubrid
```

### Debugging CUBRID within Kubernetes cluster
* Docker images created using [Dockerfile] from this directory will have `vim` and `gdb` installed
* In case of a crash, a core file will be generated and stored in `/var/lib/cubrid/<core-file>`
    * `/var/lib/cubrid` directory is externally mounted, so core files will be persisted after cluster is destroyed
* If `--install-sources` argument is used on `build-docker-image` target from build.sh script,
then CUBRID sources will be installed under `src` directory in install directory (default install directory is `/opt/cubrid`) on build time
    * Once installed, src directory will be loaded automatically when starting gdb 

### Connect to CUBRID 
* Using jdbc:
    * First get the ip of the minikube machine by running
        ```bash
        $ minikube ip 
        192.168.99.108
        ```
    * Then get the randomly generated port used by Kubernetes for LoadBalancer
        ```bash
        $ kubectl get all -l app=cubrid
        NAME           READY   STATUS    RESTARTS   AGE
        pod/cubrid-0   1/1     Running   0          77m
        pod/cubrid-1   1/1     Running   0          77m
        
        NAME                  TYPE           CLUSTER-IP     EXTERNAL-IP   PORT(S)           AGE
        service/cubrid        ClusterIP      None           <none>        33000/TCP         77m
        service/cubrid-read   LoadBalancer   10.107.35.76   <pending>     33000:32201/TCP   77m
        ```
```java
Connection connection = DriverManager.getConnection("jdbc:cubrid:192.168.99.108:32201:test_db:::?charset=utf8", "dba", "");
```

* Using csql: 
```bash
$ kubectl exec -it cubrid-0 -- /bin/bash
[cubrid@cubrid-0 /]$ csql -C -udba ${DB_NAME}@127.0.0.1 -c "SELECT 1"
=== <Result of SELECT Command in Line 1> ===

            1
=============
            1

1 row selected. (0.020884 sec) Committed.
[cubrid@cubrid-0 /]$ 
```

[//]: #
   [docker]: <https://docs.docker.com/install>
   [kubectl]: <https://kubernetes.io/docs/tasks/tools/install-kubectl>
   [minikube]: <https://kubernetes.io/docs/tasks/tools/install-minikube>
   [Dockerfile]: <https://github.com/CUBRID/cubrid/tree/develop/contrib/cloud/Dockerfile>
   [use-local-images-by-re-using-the-docker-daemon]: <https://kubernetes.io/docs/setup/minikube/#use-local-images-by-re-using-the-docker-daemon>
