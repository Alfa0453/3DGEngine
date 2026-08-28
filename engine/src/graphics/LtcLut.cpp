#include "engine/graphics/LtcLut.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <vector>
namespace engine {
namespace { unsigned int gMatrix=0,gAmplitude=0;int gUsers=0; }
LtcLut::LtcLut(){++gUsers;if(gMatrix&&gAmplitude){m_matrix=gMatrix;m_amplitude=gAmplitude;return;}
    constexpr int size=64;std::vector<float> matrix(size*size*4),amplitude(size*size*2);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){const float roughness=(x+0.5f)/size,nDotV=(y+0.5f)/size;
        const float alpha=std::max(roughness*roughness,0.0025f);const std::size_t i=static_cast<std::size_t>(y*size+x);
        matrix[i*4]=1.0f/std::sqrt(alpha);matrix[i*4+1]=0.0f;matrix[i*4+2]=(1.0f-nDotV)*(1.0f-alpha)*0.35f;matrix[i*4+3]=std::sqrt(alpha);
        amplitude[i*2]=std::clamp(1.0f-0.35f*roughness,0.0f,1.0f);amplitude[i*2+1]=0.04f+(1.0f-roughness)*0.06f;}
    glGenTextures(1,&gMatrix);glBindTexture(GL_TEXTURE_2D,gMatrix);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,size,size,0,GL_RGBA,GL_FLOAT,matrix.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenTextures(1,&gAmplitude);glBindTexture(GL_TEXTURE_2D,gAmplitude);glTexImage2D(GL_TEXTURE_2D,0,GL_RG16F,size,size,0,GL_RG,GL_FLOAT,amplitude.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    m_matrix=gMatrix;m_amplitude=gAmplitude;}
LtcLut::~LtcLut(){if(--gUsers==0){if(gMatrix)glDeleteTextures(1,&gMatrix);if(gAmplitude)glDeleteTextures(1,&gAmplitude);gMatrix=0;gAmplitude=0;}m_matrix=0;m_amplitude=0;}
void LtcLut::Bind(unsigned int matrixUnit,unsigned int amplitudeUnit)const{glActiveTexture(GL_TEXTURE0+matrixUnit);glBindTexture(GL_TEXTURE_2D,m_matrix);glActiveTexture(GL_TEXTURE0+amplitudeUnit);glBindTexture(GL_TEXTURE_2D,m_amplitude);}
}
