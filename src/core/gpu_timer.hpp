#pragma once
#include <glad/gl.h>

// GPU time of a pass from GL_TIME_ELAPSED queries. Results are read kLag
// frames later, so measuring never stalls the CPU on the GPU; a slot that is
// still in flight is skipped rather than overwritten.
class GpuTimer {
public:
    void init() { glCreateQueries(GL_TIME_ELAPSED, kLag, q_); }
    void destroy() { glDeleteQueries(kLag, q_); }

    void begin() {
        for (int i = 0; i < kLag; ++i) collect(i);
        skipped_ = pending_[head_];
        if (!skipped_) glBeginQuery(GL_TIME_ELAPSED, q_[head_]);
    }

    void end() {
        if (skipped_) return;
        glEndQuery(GL_TIME_ELAPSED);
        pending_[head_] = true;
        head_           = (head_ + 1) % kLag;
    }

    double takeAverage() {
        const double avg = samples_ ? sumMs_ / samples_ : 0.0;
        sumMs_           = 0.0;
        samples_         = 0;
        return avg;
    }

private:
    static constexpr int kLag = 4;

    void collect(int i) {
        if (!pending_[i]) return;
        GLint ready = 0;
        glGetQueryObjectiv(q_[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) return;
        GLuint64 ns = 0;
        glGetQueryObjectui64v(q_[i], GL_QUERY_RESULT, &ns);
        sumMs_ += double(ns) * 1e-6;
        ++samples_;
        pending_[i] = false;
    }

    GLuint q_[kLag]{};
    bool   pending_[kLag]{};
    int    head_    = 0;
    bool   skipped_ = false;
    double sumMs_   = 0.0;
    int    samples_ = 0;
};
