// Copyright (c) 2018, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information

#pragma once

#include <string>

const std::string nonWindowsAsciiArt =
    "\n                                         \n"
    "                  ,@@@@@@@,                \n"
    "      ,,,.   ,@@@@@@/@@,  .oo8888o.        \n"
    "   ,&%%&%&&%,@@@@@/@@@@@@,8888\88/8o       \n"
    "  ,%&\%&&%&&%,@@@\@@@/@@@88\88888/88'      \n"
    "  %&&%&%&/%&&%@@\@@/ /@@@88888\88888'      \n"
    "  %&&%/ %&%%&&@@\ V /@@' `88\8 `/88'       \n"
    "  `&%\ ` /%&'    |.|        \ '|8'         \n"
    "      |o|        | |         | |           \n"
    "      |.|        | |         | |           \n"
    " \\/ ._\//_/__/  ,\_//__\\/.  \_//__/_     \n";


const std::string windowsAsciiArt =
    "\n                                         \n"
    "                  ,@@@@@@@,                \n"
    "      ,,,.   ,@@@@@@/@@,  .oo8888o.        \n"
    "   ,&%%&%&&%,@@@@@/@@@@@@,8888\88/8o       \n"
    "  ,%&\%&&%&&%,@@@\@@@/@@@88\88888/88'      \n"
    "  %&&%&%&/%&&%@@\@@/ /@@@88888\88888'      \n"
    "  %&&%/ %&%%&&@@\ V /@@' `88\8 `/88'       \n"
    "  `&%\ ` /%&'    |.|        \ '|8'         \n"
    "      |o|        | |         | |           \n"
    "      |.|        | |         | |           \n"
    " \\/ ._\//_/__/  ,\_//__\\/.  \_//__/_     \n";

/* Windows has some characters it won't display in a terminal. If your ascii
   art works fine on Windows and Linux terminals, just replace 'asciiArt' with
   the art itself, and remove these two #ifdefs and above ascii arts */
#ifdef _WIN32

const std::string asciiArt = windowsAsciiArt;

#else
const std::string asciiArt = nonWindowsAsciiArt;
#endif
