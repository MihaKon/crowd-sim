#pragma once
#include <glad/gl.h>

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>

namespace gl {

inline std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Minimal #include "file" support (relative to the including file), so
// shaders can share code. Not recursive-safe against cycles; keep it simple.
inline std::string preprocess(const std::string& path) {
    const std::string dir = path.substr(0, path.find_last_of('/') + 1);
    std::istringstream in(readFile(path));
    std::string out, line;
    while (std::getline(in, line)) {
        if (line.rfind("#include \"", 0) == 0) {
            const size_t a = line.find('"') + 1, b = line.find('"', a);
            out += preprocess(dir + line.substr(a, b - a));
        } else {
            out += line;
        }
        out += '\n';
    }
    return out;
}

// `defines` (e.g. "#define PASS_SIM\n") is inserted right after the #version line,
// so one source file can produce several specialised programs.
inline GLuint compileShader(GLenum type, const std::string& path, const std::string& defines = "") {
    std::string src = preprocess(path);
    if (!defines.empty()) src.insert(src.find('\n') + 1, defines);
    const char* p = src.c_str();
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string log(std::size_t(len), '\0');
        glGetShaderInfoLog(s, len, nullptr, log.data());
        glDeleteShader(s);
        throw std::runtime_error(path + ":\n" + log);
    }
    return s;
}

inline GLuint linkProgram(std::initializer_list<GLuint> shaders) {
    const GLuint p = glCreateProgram();
    for (GLuint s : shaders) glAttachShader(p, s);
    glLinkProgram(p);
    for (GLuint s : shaders) {
        glDetachShader(p, s);
        glDeleteShader(s);
    }

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::string log(std::size_t(len), '\0');
        glGetProgramInfoLog(p, len, nullptr, log.data());
        glDeleteProgram(p);
        throw std::runtime_error("link failed:\n" + log);
    }
    return p;
}

} // namespace gl
