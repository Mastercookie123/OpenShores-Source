#pragma once
#include <QtGui/qopengl.h>
#include <QString>
class AuOpenGLFunctions;
class AuGLEffects;
class AuMatrix4x4;
// All 3D submissions cross this boundary, not a list of selected entity types.
bool AuFilamentDraw(AuOpenGLFunctions&,GLenum,GLint,GLsizei,GLenum,const void*,GLsizei);
void AuFilamentBufferChanged(AuOpenGLFunctions&,GLenum);
void AuFilamentTextureChanged(AuOpenGLFunctions&,GLenum);
void AuFilamentDeleted(AuOpenGLFunctions&,bool,GLsizei,const GLuint*);
void AuFilamentClear(AuOpenGLFunctions&,GLbitfield);
void AuFilamentShader(GLuint,const QString&);
bool AuFilamentWorldBegin(AuGLEffects&,const AuMatrix4x4&);
void AuFilamentWorldEnd(AuGLEffects&);
class AuFilamentWorldScope {
    AuGLEffects& gl;bool active;
public:
    AuFilamentWorldScope(AuGLEffects& renderer,const AuMatrix4x4& projection):gl(renderer),active(AuFilamentWorldBegin(renderer,projection)){}
    ~AuFilamentWorldScope(){if(active)AuFilamentWorldEnd(gl);}
};
