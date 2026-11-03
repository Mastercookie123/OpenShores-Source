#include "au3dsglsl.h"
#include "aubodysurface.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QDir>
#include <iostream>

int main(int argc, char** argv)
{
    QGuiApplication app(argc,argv);
    if(argc != 3) { std::cerr << "Usage: RenderCharacterProbe BODY_PART OUTPUT_DIRECTORY\n"; return 2; }
    Au3dsGLSLModel model;
    if(!model.Load(QString::fromLocal8Bit(argv[1]))) return 3;
    Au3dsGLSLTriBodyList original;
    for(auto& object : model.m_Objects)
        object.LoadModel(original, model.m_Materials, false, model.m_ModelType, GL_ONE_MINUS_SRC_ALPHA);
    if(original.isEmpty()) return 4;
    auto refined = original;
    AuBodySurface::Refine(refined,2);
    std::cout << "Actual body asset: " << argv[1] << "; triangles " << original.size()
        << " -> " << refined.size() << '\n';
    QSurfaceFormat format; format.setVersion(3,2); format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context; context.setFormat(format);
    if(!context.create()) return 5;
    QOffscreenSurface surface; surface.setFormat(context.format()); surface.create();
    if(!context.makeCurrent(&surface)) return 6;
    AuOpenGLFunctions gl; if(!gl.initializeOpenGLFunctions()) return 7;
    QOpenGLShaderProgram shader;
    if(!shader.addShaderFromSourceCode(QOpenGLShader::Vertex,
        "#version 150\nin vec3 position; in vec3 normal; uniform mat4 mvp; uniform mat3 normalMatrix; out vec3 N;"
        "void main(){N=normalMatrix*normal;gl_Position=mvp*vec4(position,1);}")) return 8;
    if(!shader.addShaderFromSourceCode(QOpenGLShader::Fragment,
        "#version 150\nin vec3 N;out vec4 color;void main(){vec3 n=normalize(N);"
        "float d=max(dot(n,normalize(vec3(-.5,.8,1.))),0.);"
        "color=vec4(vec3(.62,.43,.30)*(.22+.78*d),1);}")) return 9;
    if(!shader.link()) return 10;
    AuPoint min = original[0].m_V0, max = min;
    for(const auto& t : original)
        for(const auto* v : {&t.m_V0,&t.m_V1,&t.m_V2})
        {
            min.x=qMin(min.x,v->x);min.y=qMin(min.y,v->y);min.z=qMin(min.z,v->z);
            max.x=qMax(max.x,v->x);max.y=qMax(max.y,v->y);max.z=qMax(max.z,v->z);
        }
    double size=qMax(max.x-min.x,qMax(max.y-min.y,max.z-min.z));
    AuPoint center=(min+max)*.5;
    QMatrix4x4 transform; transform.perspective(35,1,.01,100);
    transform.lookAt(QVector3D(2.5,-4,1.6),QVector3D(0,0,0),QVector3D(0,0,1));
    QMatrix4x4 modelMatrix;modelMatrix.scale(float(2./size));
    modelMatrix.translate(float(-center.x),float(-center.y),float(-center.z));
    QOpenGLFramebufferObject fbo(800,800,QOpenGLFramebufferObject::CombinedDepthStencil);
    if(!fbo.isValid()) return 11;
    QString directory=QString::fromLocal8Bit(argv[2]);QDir().mkpath(directory);
    int index=0;
    for(const auto* mesh : {&original,&refined})
    {
        QVector<float> vertices;
        for(const auto& t : *mesh)
            for(const auto* v : {&t.m_V0,&t.m_V1,&t.m_V2})
                vertices << float(v->x) << float(v->y) << float(v->z)
                    << float(v->m_N.x) << float(v->m_N.y) << float(v->m_N.z);
        GLuint vao,buffer;gl.glGenVertexArrays(1,&vao);gl.glBindVertexArray(vao);
        gl.glGenBuffers(1,&buffer);gl.glBindBuffer(GL_ARRAY_BUFFER,buffer);
        gl.glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.constData(),GL_STATIC_DRAW);
        fbo.bind();gl.glViewport(0,0,800,800);gl.glEnable(GL_DEPTH_TEST);
        gl.glClearColor(.035f,.045f,.06f,1);gl.glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        shader.bind();shader.setUniformValue("mvp",transform*modelMatrix);
        shader.setUniformValue("normalMatrix",modelMatrix.normalMatrix());
        int p=shader.attributeLocation("position"),n=shader.attributeLocation("normal");
        gl.glEnableVertexAttribArray(p);gl.glVertexAttribPointer(p,3,GL_FLOAT,GL_FALSE,6*sizeof(float),nullptr);
        gl.glEnableVertexAttribArray(n);gl.glVertexAttribPointer(n,3,GL_FLOAT,GL_FALSE,6*sizeof(float),(void*)(3*sizeof(float)));
        gl.glDrawArrays(GL_TRIANGLES,0,vertices.size()/6);gl.glFinish();
        if(!fbo.toImage().save(directory+(index++ ? "/refined.png" : "/original.png"))) return 12;
        gl.glDeleteBuffers(1,&buffer);gl.glDeleteVertexArrays(1,&vao);
    }
    return 0;
}
