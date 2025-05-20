#!/bin/bash -u
# We use set -e and bash with -u to bail on first non zero exit code of any
# processes launched or upon any unbound variable.
# We use set -x to print commands before running them to help with
# debugging.
set -ex

echo "START BUILDING (HOST)"

echo "Cleaning previously built binary"
rm -f release-build/xahaud

BUILD_CORES=$(echo "scale=0 ; `nproc` / 1.337" | bc)

if [[ "$GITHUB_REPOSITORY" == "" ]]; then
  #Default
  BUILD_CORES=8
fi

EXIT_IF_CONTAINER_RUNNING=${EXIT_IF_CONTAINER_RUNNING:-1}
# Ensure still works outside of GH Actions by setting these to /dev/null
# GA will run this script and then delete it at the end of the job
JOB_CLEANUP_SCRIPT=${JOB_CLEANUP_SCRIPT:-/dev/null}
NORMALIZED_WORKFLOW=$(echo "$GITHUB_WORKFLOW" | tr -c 'a-zA-Z0-9' '-')
NORMALIZED_REF=$(echo "$GITHUB_REF" | tr -c 'a-zA-Z0-9' '-')
CONTAINER_NAME="xahaud_cached_builder_${NORMALIZED_WORKFLOW}-${NORMALIZED_REF}"

# Check if the container is already running
if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    echo "⚠️ A running container (${CONTAINER_NAME}) was detected."

    if [[ "$EXIT_IF_CONTAINER_RUNNING" -eq 1 ]]; then
        echo "❌ EXIT_IF_CONTAINER_RUNNING is set. Exiting."
        exit 1
    else
        echo "🛑 Stopping the running container: ${CONTAINER_NAME}"
        docker stop "${CONTAINER_NAME}"
    fi
fi

echo "-- BUILD CORES:       $BUILD_CORES"
echo "-- GITHUB_REPOSITORY: $GITHUB_REPOSITORY"
echo "-- GITHUB_SHA:        $GITHUB_SHA"
echo "-- GITHUB_RUN_NUMBER: $GITHUB_RUN_NUMBER"
echo "-- CONTAINER_NAME:    $CONTAINER_NAME"

which docker 2> /dev/null 2> /dev/null
if [ "$?" -eq "1" ]
then
  echo 'Docker not found. Install it first.'
  exit 1
fi

stat .git 2> /dev/null 2> /dev/null
if [ "$?" -eq "1" ]
then
  echo 'Run this inside the source directory. (.git dir not found).'
  exit 1
fi

STATIC_CONTAINER=$(docker ps -a | grep $CONTAINER_NAME |wc -l)

#if [[ "$STATIC_CONTAINER" -gt "0" && "$GITHUB_REPOSITORY" != "" ]]; then
if false; then
  echo "Static container, execute in static container to have max. cache"
  docker start $CONTAINER_NAME
  docker exec -i $CONTAINER_NAME /hbb_exe/activate-exec bash -x /io/build-core.sh "$GITHUB_REPOSITORY" "$GITHUB_SHA" "$BUILD_CORES" "$GITHUB_RUN_NUMBER"
  docker stop $CONTAINER_NAME
else
  echo "No static container, build on temp container"
  rm -rf release-build;
  mkdir -p release-build;

  if [[ "$GITHUB_REPOSITORY" == "" ]]; then
    # Non GH, local building
    echo "Non-GH runner, local building, temp container"
    docker run -i --user 0:$(id -g) --rm -v /data/builds:/data/builds -v `pwd`:/io --network host ghcr.io/foobarwidget/holy-build-box-x64 /hbb_exe/activate-exec bash -x /io/build-full.sh "$GITHUB_REPOSITORY" "$GITHUB_SHA" "$BUILD_CORES" "$GITHUB_RUN_NUMBER"
  else
    # GH Action, runner
    echo "GH Action, runner, clean & re-create create persistent container"
    docker rm -f $CONTAINER_NAME
    echo "echo 'Stopping container: $CONTAINER_NAME'" >> "$JOB_CLEANUP_SCRIPT"
    echo "docker stop --time=15 \"$CONTAINER_NAME\" || echo 'Failed to stop container or container not running'" >> "$JOB_CLEANUP_SCRIPT"
    docker run -di --user 0:$(id -g) --name $CONTAINER_NAME -v /data/builds:/data/builds -v `pwd`:/io --network host ghcr.io/foobarwidget/holy-build-box-x64 /hbb_exe/activate-exec bash
    docker exec -i $CONTAINER_NAME /hbb_exe/activate-exec bash -x /io/build-full.sh "$GITHUB_REPOSITORY" "$GITHUB_SHA" "$BUILD_CORES" "$GITHUB_RUN_NUMBER"
    docker stop $CONTAINER_NAME
  fi
fi

echo "DONE BUILDING (HOST)"
