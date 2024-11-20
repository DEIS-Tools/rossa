
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

In the `ext` folder is the definition of interface used by the model to communicate with the scheduler. Schedulers must only implement the functions prefixed with `custom`. 

Schedulers can make use of the global `network` object and the `parameters`, `flows` and `topology` fields. It should not read the `buffers` field directly since that is considered hidden information from the scheduler. It may; however, use the `PortLoad::getLoad` and `PortLoad::getTotalPortLoad`.

Care should be taken to ensure wrapping and avoid overflow. As an example, the output parameter `choice_phase` for `customGetScheduleChoice` should always be in a future phase since using the current passed `phase_i` would mean the package would wait an entire cycle. But be sure to value is `0 <= choice_phase < NUM_PHASES`.

# Schedule Types

Some of the schedule types listed here makes use of the files in the `tgraph` folder. The shared helper implementation there expands the topology over time to form a "temporal graph". For example, Node N may have two ports P1 and P2. Ports P1 and P2 are connected to different nodes in different phases. By adding the current phase to a graph then Node N in phase I: (N, I) is connected to all its ports P1 and P2 for all phases such that there is an edge from (N, I) to (P1, J) and from (N, I) to (P2, J) for all 0 <= J < NUM_PHASES. By considering the delays and traversals involved we can derive schedules.

## Environment Variables

The schedulers have internal settings that are configured via environment variables. They must be set before starting UPPAAL (or verifyta) such that they are in the defined inside process when the shared library is loaded.

## Fixed schedule

Folder: fixed

Quickest: Considering the topology of each phase, then for each flow compute paths from ingress to egress minimize number of phase shifts (simulation steps). occuring.
Fewest hops: Same considerations as above, but minimize number of hops (number of switches passed through)

## Capacity Consideration

Folder: capacity

Will compute shortest paths to each egress node for all ports. Then select K (1 < K <= 5 typically) neighbours based on the most optimal paths. During simulation will then take into account the current load: if the load is greater than some treshold it will change direct packets to the next-preferred port.

The following environment variables, with their defaults shown, are used to set K, the treshold and overall approach.

```
CAPACITY_NUM_PATHS=2
CAPACITY_APPROACH=QUICKEST
CAPACITY_TRESHOLD=0.7
```

Two variants based on optimizing for number simulation steps (QUICKEST) or number of hops (FEWEST_HOPS).

Requires number of switches in the work >= K.

## Random Fixed Choice

Folder: rnd_choice

Combination of fixed and capacity but with no load consideration. Constructs the optimal paths and select best K paths using unique ports and then randomly select among them during each step of the simulation.

The following environment variables, with their defaults shown, are used to set K and overall approach.

```
CAPACITY_NUM_PATHS=2
CAPACITY_APPROACH=QUICKEST
```

Two variants based on optimizing for number simulation steps or number of hops.
