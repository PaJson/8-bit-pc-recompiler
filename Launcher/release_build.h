#ifndef RELEASE_BUILD_H
#define RELEASE_BUILD_H
/* Build one game in a release folder (release_build.c). root = the release folder, gdir = its
   Games\<platform>\<slug> folder, rom = the ROM file. Output on stdout. 0 = built. */
int release_build(const char *root, const char *gdir, const char *rom);
#endif
