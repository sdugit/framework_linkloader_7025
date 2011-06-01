#include "ELFObject.h"

#include "utils/serialize.h"

#include <llvm/ADT/OwningPtr.h>
#include <iomanip>
#include <iostream>

#include <elf.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <map>
#include <stdio.h>

using namespace serialization;
using namespace std;

bool open_mmap_file(char const *filename,
                    int &fd,
                    unsigned char const *&image,
                    size_t &size);

void close_mmap_file(int fd,
                     unsigned char const *image,
                     size_t size);

void dump_file(unsigned char const *image, size_t size);

int main(int argc, char **argv) {
  // Check arguments
  if (argc < 2) {
    llvm::errs() << "USAGE: " << argv[0] << " [ELFObjectFile]\n";
    exit(EXIT_FAILURE);
  }

  // Filename from argument
  char const *filename = argv[1];

  // Open the file
  int fd = -1;
  unsigned char const *image = NULL;
  size_t image_size = 0;

  if (!open_mmap_file(filename, fd, image, image_size)) {
    llvm::errs() << "ERROR: Unable to open the file: " << filename << "\n";
    exit(EXIT_FAILURE);
  }

  // Dump the file
  dump_file(image, image_size);

  // Close the file
  close_mmap_file(fd, image, image_size);

  return EXIT_SUCCESS;
}

void *find_sym(char const *name_, void *context) {
  std::string name = name_;
  std::map<std::string, void *> fptr;

#define DEF_FUNC(FUNC) \
  fptr.insert(make_pair(#FUNC, (void *)&FUNC));

  DEF_FUNC(rand);
  DEF_FUNC(printf);
  DEF_FUNC(scanf);
  DEF_FUNC(srand);
  DEF_FUNC(time);

#undef DEF_FUNC

  fptr.insert(make_pair("__isoc99_scanf", (void*)scanf));

  if (fptr.count(name) > 0) {
    return fptr[name];
  }
  assert(0 && "Can't find symbol.");
  return 0;
}

template <size_t Bitwidth, typename Archiver>
void dump_object(Archiver &AR) {
  llvm::OwningPtr<ELFObject<Bitwidth> > object(ELFObject<Bitwidth>::read(AR));

  if (!object) {
    cerr << "ERROR: Unable to load object" << endl;
  }

  object->print();
  out().flush();

  ELFSectionSymTab<Bitwidth> *symtab =
    static_cast<ELFSectionSymTab<Bitwidth> *>(
        object->getSectionByName(".symtab"));

  object->relocate(find_sym, 0);

  void *main_addr = symtab->getByName("main")->getAddress();
  out() << "main address: " << main_addr << "\n";
  out().flush();
  ((int (*)(int, char **))main_addr)(0,0);
}

template <typename Archiver>
void dump_file_from_archive(bool is32bit, Archiver &AR) {
  if (is32bit) {
    dump_object<32>(AR);
  } else {
    dump_object<64>(AR);
  }
}

void dump_file(unsigned char const *image, size_t size) {
  if (size < EI_NIDENT) {
    cerr << "ERROR: ELF identification corrupted." << endl;
    return;
  }

  if (image[EI_DATA] != ELFDATA2LSB && image[EI_DATA] != ELFDATA2MSB) {
    cerr << "ERROR: Unknown endianness." << endl;
    return;
  }

  if (image[EI_CLASS] != ELFCLASS32 && image[EI_CLASS] != ELFCLASS64) {
    cerr << "ERROR: Unknown machine class." << endl;
    return;
  }

  bool isLittleEndian = (image[EI_DATA] == ELFDATA2LSB);
  bool is32bit = (image[EI_CLASS] == ELFCLASS32);

  if (isLittleEndian) {
    archive_reader_le AR(image, size);
    dump_file_from_archive(is32bit, AR);
  } else {
    archive_reader_be AR(image, size);
    dump_file_from_archive(is32bit, AR);
  }
}

bool open_mmap_file(char const *filename,
                    int &fd,
                    unsigned char const *&image,
                    size_t &size) {
  // Open the file in readonly mode
  fd = open(filename, O_RDONLY);
  if (fd < 0) {
    return false;
  }

  // Query the file size
  struct stat sb;
  if (fstat(fd, &sb) != 0) {
    close(fd);
    return false;
  }

  size = (size_t)sb.st_size;

  // Map the file image
  image = static_cast<unsigned char const *>(
    mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0));

  if (image == NULL || image == MAP_FAILED) {
    close(fd);
    return false;
  }

  return true;
}

void close_mmap_file(int fd,
                     unsigned char const *image,
                     size_t size) {
  if (image) {
    munmap((void *)image, size);
  }

  if (fd >= 0) {
    close(fd);
  }
}
