#ifndef REGINA_FILEIO_H_INCLUDED
#define REGINA_FILEIO_H_INCLUDED

#include <cstdio>

#include "abstract_fileio.h"

template<bool writeOnly, bool binary>
class FileIO : public AbstractFileIO<writeOnly, binary> {
public:
    typedef AbstractFileIO<writeOnly, binary> Super;

    inline FileIO() { };

    inline FileIO(const char *filename) :
        Super(filename){

    }

    inline ~FileIO(void) { };

    inline FileIO &operator=(const FileIO &&rhs) {
        Super::operator=(rhs);
        return *this;
    }
};


template<bool writeOnly>
class FileIO<writeOnly, false> : public AbstractFileIO<writeOnly, false> {
public:
    typedef AbstractFileIO<writeOnly, false> Super;

    inline FileIO() { };

    inline FileIO(const char *filename) :
        Super(filename) {

    }

    inline ~FileIO(void) {

    }

    inline FileIO &operator=(const FileIO &&rhs) {
        Super::operator=(rhs);
        return *this;
    }

    void Print(FILE *const f, const AbstractFileIO<writeOnly, false>::RefType refType, const void *ref);
};


template<bool writeOnly>
inline void FileIO<writeOnly, false>::Print(FILE *const f, const AbstractFileIO<writeOnly, false>::RefType refType, const void *ref) {
    if (refType == Super::RefType::MemRef) {
        const typename Super::MemRef_t *memRef = reinterpret_cast<const typename Super::MemRef_t *>(ref);

        if (memRef->is_write) {
            std::fprintf(f, "MEM WRITE @ %p %s of size %d to %p\n", memRef->instr, memRef->instrSym.c_str(), memRef->size, memRef->data);
        } else {
            std::fprintf(f, "MEM READ @ %p %s of size %d to %p\n", memRef->instr, memRef->instrSym.c_str(), memRef->size, memRef->data);
        }

    } else {
        const typename Super::CallRetRef_t *callRef = reinterpret_cast<const typename Super::CallRetRef_t *>(ref);

        if (refType == Super::RefType::CallRef) {
            std::fprintf(f, "CALL @ %p %s\n", callRef->instr, callRef->instrSym.c_str());
            std::fprintf(f, "\t to %p %s\n", callRef->target, callRef->targetSym.c_str());
        } else if (refType == Super::RefType::CallIndRef) {
            std::fprintf(f, "CALL IND @ %p %s\n", callRef->instr, callRef->instrSym.c_str());
            std::fprintf(f, "\t to %p %s\n", callRef->target, callRef->targetSym.c_str());
        } else if (refType == Super::RefType::RetRef) {
            std::fprintf(f, "RET @ %p %s\n", callRef->instr, callRef->instrSym.c_str());
            std::fprintf(f, "\t to %p %s\n", callRef->target, callRef->targetSym.c_str());
        }
    }
}

//template<bool writeOnly>
//inline void FileIO<writeOnly, false>::Print(const typename Super::RefType refType, const void *ref) {
//    if (refType == MemRef) {
//        MemRef_t *memRef = reinterpret_cast<MemRef_t *>(ref);
//
//        if (memRef.is_write) {
//            std::fprintf(this->f, "\t\t\t mem write @ %p %u %p\n", memRef.instr, memRef.size, memRef.data);
//        } else {
//            std::fprintf(this->f, "\t\t\t mem read @ %p %u %p\n", memRef.instr, memRef.size, memRef.data);
//        }
//    } else {
//        CallRetRef_t *callRef = reinterpret_cast<CallRetRef_t *>(ref);
//        if (refType == CallRef) {
//            std::fprintf(this->f, "Call @ %p %s\n", callRef.instr, callRef.instrSym);
//            std::fprintf(this->f, "\t to %p %s\n", callRef.target, callRef.targetSym);
//        } else if (refType == CallIndRef) {
//            std::fprintf(this->f, "CallInd @ %p %s\n", callRef.instr, callRef.instrSym);
//            std::fprintf(this->f, "\t to %p %s\n", callRef.target, callRef.targetSym);
//        } else if (refType == RetRef) {
//            std::fprintf(this->f, "Return @ %p %s\n", callRef.instr, callRef.instrSym);
//            std::fprintf(this->f, "\t to %p %s\n", callRef.target, callRef.targetSym);
//        }
//    }
//}


template<bool writeOnly>
class FileIO<writeOnly, true> : public AbstractFileIO<writeOnly, true> {
public:
    typedef AbstractFileIO<writeOnly, true> Super;

    inline FileIO() { };

    inline FileIO(const char *filename) :
        Super(filename) {

    }

    inline ~FileIO(void) {

    }

    inline FileIO &operator=(const FileIO &&rhs) {
        Super::operator=(std::move(rhs));
        return *this;
    }

    void Print(FILE *const f, const AbstractFileIO<writeOnly, true>::RefType refType, const void *ref);
};


template<bool writeOnly>
inline void FileIO<writeOnly, true>::Print(FILE *const f, const AbstractFileIO<writeOnly, true>::RefType refType, const void *ref) {
    if (refType == Super::RefType::MemRef) {
        const typename Super::MemRef_t *memRef = reinterpret_cast<const typename Super::MemRef_t *>(ref);

        std::fwrite(&refType, sizeof(AbstractFileIO<writeOnly, true>::RefType), 1, f);
        if (memRef->is_write) {
            bool tmp = true;
            std::fwrite(&tmp, sizeof(bool), 1, f);
        } else {
            bool tmp = false;
            std::fwrite(&tmp, sizeof(bool), 1, f);
        }
        std::fwrite(&(memRef->instr), sizeof(size_t), 1, f);
        std::fwrite(&(memRef->size), sizeof(unsigned int), 1, f);
        std::fwrite(&(memRef->data), sizeof(size_t), 1, f);

    } else {
        const typename Super::CallRetRef_t *callRef = reinterpret_cast<const typename Super::CallRetRef_t *>(ref);
        if (refType == Super::RefType::CallRef) {
            typename Super::RefType tmp = Super::RefType::CallRef;
            std::fwrite(&tmp, sizeof(char), 1, f);
        } else if (refType == Super::RefType::CallIndRef) {
            typename Super::RefType tmp = Super::RefType::CallIndRef;
            std::fwrite(&tmp, sizeof(char), 1, f);
        } else if (refType == Super::RefType::RetRef) {
            typename Super::RefType tmp = Super::RefType::RetRef;
            std::fwrite(&tmp, sizeof(char), 1, f);
        }
        std::fwrite(&(callRef->instr), sizeof(size_t), 1, f);
        std::fwrite(&(callRef->target), sizeof(size_t), 1, f);
        std::fwrite((callRef->instrSym.c_str()), std::strlen(callRef->instrSym.c_str()), 1, f);
        std::fwrite((callRef->targetSym.c_str()), std::strlen(callRef->targetSym.c_str()), 1, f);
    }
}

#endif