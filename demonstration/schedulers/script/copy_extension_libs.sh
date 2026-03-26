DEST_FOLDER=../precompiled
cmake --build build -j4
cp ./build/fixed/libfixed.so $DEST_FOLDER/libfixed.so
cp ./build/rotor_lb/librotor_lb.so $DEST_FOLDER/librotor_lb.so
cp ./build/valiant/libvaliant.so $DEST_FOLDER/libvaliant.so
