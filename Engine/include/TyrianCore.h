/*
 * MetalTyrian: public interface of the TyrianCore target (OpenTyrian2000's
 * game code). SwiftPM needs a public headers folder for every C target; the
 * engine's own headers stay private in src/. The app starts the game through
 * SDLShim's metaltyrian_host.h, which calls this.
 */
#ifndef TYRIANCORE_H
#define TYRIANCORE_H

int tyrian_main(int argc, char *argv[]);

#endif
