#ifndef REGINA_ABSTRACT_FILEIO_H_INCLUDED
#define REGINA_ABSTRACT_FILEIO_H_INCLUDED

#include <cstdio>
#include <string>

template<bool writeOnly, bool binary>
class AbstractFileIO {
public:
    typedef enum _RefType {
        MemRef,
        CallRef,
        CallIndRef,
        RetRef
    } RefType;

    typedef struct _MemRef_t {
        bool is_write;
        unsigned int size;
        void *instr;
        void *data;
        std::string instrSym;
    } MemRef_t;

    typedef struct _CallRetRef_t {
        void *instr;
        void *target;
        std::string instrSym;
        std::string targetSym;
    } CallRetRef_t;

    virtual ~AbstractFileIO(void) {
        //std::fclose(this->f);
    }

    AbstractFileIO &operator=(const AbstractFileIO &&rhs);

    AbstractFileIO &operator=(const AbstractFileIO &rhs) = delete;
protected:
    inline AbstractFileIO(void) { };

    inline AbstractFileIO(const char *filename) {
        /*if (writeOnly) {
            if (binary) {
                this->f = std::fopen(filename, "wb");
            } else {
                this->f = std::fopen(filename, "w");
            }
        } else {
            if (binary) {
                this->f = std::fopen(filename, "rwb");
            } else {
                this->f = std::fopen(filename, "rw");
            }
        }*/
    }

    AbstractFileIO(const AbstractFileIO &rhs) = delete;

    //FILE *f;
};


template<bool writeOnly, bool binary>
inline AbstractFileIO<writeOnly, binary> &AbstractFileIO<writeOnly, binary>::operator=(const AbstractFileIO &&rhs) {
    //this->f = rhs.f;
    *this = rhs;
    return *this;
}

#endif // end ifndef REGINA_ABSTRACT_FILEIO_H_INCLUDED