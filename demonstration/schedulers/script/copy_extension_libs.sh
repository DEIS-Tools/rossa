DEST_FOLDER=../precompiled
cmake --build build -j4
cp ./build/capacity/libcapacity.so $DEST_FOLDER/libcapacity.so
cp ./build/fixed/libfixed.so $DEST_FOLDER/libfixed.so
cp ./build/rnd_choice/librnd_choice.so $DEST_FOLDER/librnd_choice.so
