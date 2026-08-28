#pragma once

namespace engine {
class LtcLut {
public:
    LtcLut();
    ~LtcLut();
    LtcLut(const LtcLut&)=delete;
    LtcLut& operator=(const LtcLut&)=delete;

    void Bind(unsigned int matrixUnit,unsigned int amplitudeUnit)const;
private:
    unsigned int m_matrix=0,m_amplitude=0;
};
}
