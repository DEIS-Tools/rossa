set -e
source ./venv/bin/activate

# Ensure latest compilation used

# Build schedulers
echo "Building demonstration schedulers"
pushd schedulers
mkdir -p build
BOOST_ROOT="$(pwd)/boost_graph" CMAKE_BUILD_TYPE=RelWithDebInfo cmake -B build/
cmake --build build -j4
popd

# Copy .so files out
BUILD_PATH=./schedulers/build
cp $BUILD_PATH/fixed/libfixed.so .
cp $BUILD_PATH/rotor_lb/librotor_lb.so .
cp $BUILD_PATH/valiant/libvaliant.so .


# Run demonstration
DEMONSTRATION_FOLDER=./instances

# PLOT_FORMAT=""
PLOT_FORMAT="--pdf"
# PLOT_FORMAT="--pgf"
# RUN_FLAGS=""
RUN_FLAGS="--force --num-workers=4"

python scripts/run_instances.py --directory $DEMONSTRATION_FOLDER/example $PLOT_FORMAT $RUN_FLAGS
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/example $PLOT_FORMAT

python scripts/run_instances.py --directory $DEMONSTRATION_FOLDER/experiment_1 $PLOT_FORMAT $RUN_FLAGS
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/experiment_1 $PLOT_FORMAT

python scripts/run_instances.py --directory $DEMONSTRATION_FOLDER/experiment_2 $PLOT_FORMAT $RUN_FLAGS
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/experiment_2 $PLOT_FORMAT

python scripts/run_instances.py --directory $DEMONSTRATION_FOLDER/experiment_1_smc_5000 $PLOT_FORMAT $RUN_FLAGS
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/experiment_1_smc_5000 $PLOT_FORMAT

python scripts/run_instances.py --directory $DEMONSTRATION_FOLDER/experiment_1_smc_6000 $PLOT_FORMAT $RUN_FLAGS
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/experiment_1_smc_6000 $PLOT_FORMAT

python scripts/run_instances.py --directory $DEMONSTRATION_FOLDER/experiment_1_smc_7000 $PLOT_FORMAT $RUN_FLAGS
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/experiment_1_smc_7000 $PLOT_FORMAT

python scripts/run_instances.py --directory $DEMONSTRATION_FOLDER/experiment_2_smc_5000 $PLOT_FORMAT $RUN_FLAGS
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/experiment_2_smc_5000 $PLOT_FORMAT

python scripts/run_instances.py --directory $DEMONSTRATION_FOLDER/experiment_2_smc_6000 $PLOT_FORMAT $RUN_FLAGS
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/experiment_2_smc_6000 $PLOT_FORMAT

python scripts/run_instances.py --directory $DEMONSTRATION_FOLDER/experiment_2_smc_7000 $PLOT_FORMAT $RUN_FLAGS
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/experiment_2_smc_7000 $PLOT_FORMAT
