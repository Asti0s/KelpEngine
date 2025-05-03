#pragma once

class Converter {
    public:
        Converter();
        ~Converter();

        Converter(const Converter&) = delete;
        Converter& operator=(const Converter&) = delete;

        Converter(Converter&&) = delete;
        Converter& operator=(Converter&&) = delete;

        void run();
};
