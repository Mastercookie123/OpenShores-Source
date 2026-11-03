#include "augleffects.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <iostream>
#include <cmath>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QSurfaceFormat format;
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    if(!context.create()) return 2;
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if(!surface.isValid() || !context.makeCurrent(&surface)) return 3;

    AuGLEffects gl;
    if(!gl.initializeOpenGLFunctions()) return 4;
    std::cout << "OpenGL renderer: " << reinterpret_cast<const char*>(gl.glGetString(GL_RENDERER)) << '\n';
    std::cout << "OpenGL version: " << reinterpret_cast<const char*>(gl.glGetString(GL_VERSION)) << '\n';
    const bool supported = context.hasExtension("GL_EXT_texture_filter_anisotropic") ||
        context.hasExtension("GL_ARB_texture_filter_anisotropic");
    float maximum = 1.f;
    if(supported) gl.glGetFloatv(0x84FF, &maximum);
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::white);
    int checks = 0;
    for(int requested : {1, 8, 16})
    {
        AuGLTexture::SetAnisotropy(requested);
        for(bool mipmapped : {false, true})
        {
            const QString id = QString("probe-%1-%2").arg(requested).arg(mipmapped);
            GLuint texture = gl.CreateTextureFromBitmap(42, id, image,
                GL_NEAREST, GL_NEAREST, mipmapped);
            if(!texture) return 5;
            gl.glBindTexture(GL_TEXTURE_2D, texture);
            GLint filter = 0;
            gl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
            if(filter != (mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST)) return 6;
            if(supported)
            {
                float actual = 0.f;
                gl.glGetTexParameterfv(GL_TEXTURE_2D, 0x84FE, &actual);
                const float expected = mipmapped ? qMin(float(requested), maximum) : 1.f;
                if(std::fabs(actual - expected) > 0.01f) return 7;
            }
            if(gl.glGetError() != GL_NO_ERROR) return 8;
            gl.DeleteTexture(42, id);
            ++checks;
        }
    }
    std::cout << "PASS: " << checks << " texture cases; anisotropy supported="
        << supported << ", hardware maximum=" << maximum << '\n';
    return 0;
}
