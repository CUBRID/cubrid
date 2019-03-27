FROM centos:latest as builder

# install required packages for building cubrid from sources
RUN set -x \
    && yum -y install ant &> /dev/null \
    && yum -y install bison &> /dev/null \
    && yum -y install cmake &> /dev/null \
    && yum -y install elfutils-devel &> /dev/null \
    && yum -y install flex &> /dev/null \
    && yum -y install gcc &> /dev/null \
    && yum -y install gcc-c++ &> /dev/null \
    && yum -y install git &> /dev/null \
    && yum -y install java-1.8.0-openjdk-devel &> /dev/null \
    && yum -y install make &> /dev/null \
    && yum -y install ncurses &> /dev/null \
    && yum -y install ncurses-devel &> /dev/null \
    && yum -y install ncurses-libs &> /dev/null \
    && yum -y install ncurses-static &> /dev/null \
    && yum -y install ncurses-term &> /dev/null \
    && yum -y install sysstat &> /dev/null \
    && yum -y install systemtap-sdt-devel &> /dev/null

ENV CUBRID /opt/cubrid
ENV CUBRID_BASE_DIR /cubrid
ENV CUBRID_BUILD_DIR $CUBRID_BASE_DIR/build

# build arguments
ARG BUILD_TYPE=Debug
ARG MAKEFLAGS=-j4
ARG GIT_FORK=CUBRID
ARG GIT_REVISION=develop
ARG INSTALL_SOURCES=ON

RUN set -x && git clone https://github.com/$GIT_FORK/cubrid.git &> /dev/null

WORKDIR $CUBRID_BASE_DIR
RUN set -x && git checkout $GIT_REVISION

# build cubrid from sources
WORKDIR $CUBRID_BUILD_DIR
RUN set -x \
    && cmake -DCMAKE_INSTALL_PREFIX=$CUBRID \
             -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
             -DWITH_SOURCES=$INSTALL_SOURCES \
             -DUNIT_TESTS=OFF .. &> /dev/null \
    && make install &> /dev/null

# start a new stage, for more details check https://docs.docker.com/develop/develop-images/multistage-build
FROM centos:latest

ARG UID=985
ARG DB_NAME=cubdb
ARG DB_LOCALE=en_US
ARG DB_VOLUME_SIZE=128M
ARG CUBRID_COMPONENTS=ALL

ENV CUBRID /opt/cubrid
ENV CUBRID_DATABASES /var/lib/cubrid
ENV DB_NAME $DB_NAME
ENV DB_LOCALE $DB_LOCALE
ENV DB_VOLUME_SIZE $DB_VOLUME_SIZE
ENV CUBRID_COMPONENTS $CUBRID_COMPONENTS

ADD rootfs /

RUN set -x \
    && yum -y install vim &> /dev/null \
    && yum -y install gdb &> /dev/null \
    && yum clean all &> /dev/null

RUN set -x \
    && groupadd --system --gid $UID cubrid \
    && useradd --system --no-log-init --home-dir $CUBRID --create-home --uid $UID --gid $UID cubrid

# copy install directory from builder stage to the new one
COPY --from=builder --chown=cubrid:cubrid $CUBRID $CUBRID

# configure dynamic linker run-time bindings for cubrid libraries
# ldconfig /etc/ld.so.conf.d/cubrid.conf
RUN ldconfig

# set cubrid user and cubrid group to use when running any RUN, CMD and ENTRYPOINT commands
USER cubrid:cubrid

ENV PATH $CUBRID/bin:$PATH

# store install directory (together with core file if any) and database directory
VOLUME $CUBRID_DATABASES

ENTRYPOINT ["entrypoint.sh"]

EXPOSE 33000
