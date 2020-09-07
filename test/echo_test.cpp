// Copyright (c) 2020, Christopher A. Taylor.  All rights reserved.

#include "waveshare.hpp"
using namespace lora;

#include <iostream>
#include <fstream>
using namespace std;


//------------------------------------------------------------------------------
// Entrypoint

int main(int argc, char* argv[])
{
    cout << "echo_test" << endl;

    Waveshare waveshare;

    const int channel = 0;

    if (!waveshare.Initialize(channel)) {
        cerr << "Failed to initialize";
        return -1;
    }

    cout << "Initialize succeeded" << endl;
    return 0;
}
