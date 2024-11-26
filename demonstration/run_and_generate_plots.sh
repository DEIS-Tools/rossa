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
cp $BUILD_PATH/rnd_choice/librnd_choice.so .
cp $BUILD_PATH/capacity/libcapacity.so .

# Run demonstration

DEMONSTRATION_FOLDER=./instances

python scripts/run_instances.py --num-workers 4 --directory $DEMONSTRATION_FOLDER/paper_1
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/paper_1
python scripts/run_instances.py --num-workers 4 --directory $DEMONSTRATION_FOLDER/paper_2
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/paper_2

python scripts/run_instances.py --num-workers 4 --directory $DEMONSTRATION_FOLDER/paper_3
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/paper_3
python scripts/run_instances.py --num-workers 4 --directory $DEMONSTRATION_FOLDER/paper_4
python scripts/common_plots_paper.py --output-dir $DEMONSTRATION_FOLDER/paper_4
