
# Building with CMake

## BOOST

The example schedulers uses BOOST. There is a limited subset of BOOST in the boost_graph.tgz file that can be extracted from this directory:

```sh
tar -zxf boost_graph.tgz
```

After installation the relative path to it will be `boost_graph/boost`. You can use the full *absolute path* to it in the steps below.

## Creating build tree

Create a build tree in directorty `build`. You can use `Release` or `Debug` as normal instead of `RelWithDebInfo` below if you desire.

```sh
BOOST_ROOT=/ABSOLUTE/PATH/TO/BOOST CMAKE_BUILD_TYPE=RelWithDebInfo cmake -B build/
```

Or, if you unzipped the included boost, you can use:

```sh
BOOST_ROOT="$(pwd)/boost_graph" CMAKE_BUILD_TYPE=RelWithDebInfo cmake -B build/
```

## Building

Building the build tree in directory `build`.

```sh
cmake --build build -j4
```

The extensions will be in the appropriate subfolder inside `build`, eg. `build/fixed/libfixed.so`.

# Core interface

Folder: ext

In the `ext` folder is the definition of interface used by the model to communicate with the scheduler. Schedulers must implement the three functions `init_scheduler`, `prepare_scheduler_choices`, and `scheduler_choice`. 

Schedulers can make use of the global `network` object and the `topology`, `flows` and `buffers` fields. 

# Schedule Types

Some of the schedule types listed here makes use of the files in the `tgraph` folder. The shared helper implementation there expands the topology over time to form a "temporal graph". For example, Node N may have two ports P1 and P2. Ports P1 and P2 are connected to different nodes in different phases. By adding the current phase to a graph then Node N in phase I: (N, I) is connected to all its ports P1 and P2 for all phases such that there is an edge from (N, I) to (P1, J) and from (N, I) to (P2, J) for all 0 <= J < NUM_PHASES. By considering the delays and traversals involved we can derive schedules.

## Environment Variables

The schedulers have internal settings that are configured via environment variables. They must be set before starting UPPAAL (or verifyta) such that they are defined inside the process when the shared library is loaded.

## Fixed schedule

Folder: fixed

- Quickest: Considering the topology of each phase, then for each flow compute paths from ingress to egress minimize number of phase shifts (simulation steps) occurring.
- Fewest hops: Same considerations as above, but minimize number of hops (number of switches passed through).

## Valiant

Folder: valiant

Choose a random output port on the first hop, then uses fixed quickest routing to the egress.

## RotorLB

Folder: rotor_lb

- Uniform: Implements the RotorLB algorithm from (Mellette, 2017: RotorNet). This looks at the current buffer sizes.
- Quickest: This option implements our variant of RotorLB (which we call RotorLB*), where multiple accepted offers are prioritized by quickest path to egress.
