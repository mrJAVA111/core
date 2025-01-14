/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*************************************************************************
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * Copyright 2008 by Sun Microsystems, Inc.
 *
 * OpenOffice.org - a multi-platform office productivity suite
 *
 * This file is part of OpenOffice.org.
 *
 * OpenOffice.org is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3
 * only, as published by the Free Software Foundation.
 *
 * OpenOffice.org is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License version 3 for more details
 * (a copy is included in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU Lesser General Public License
 * version 3 along with OpenOffice.org.  If not, see
 * <http://www.openoffice.org/license.html>
 * for a copy of the LGPLv3 License.
 *
 ************************************************************************/

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vcl/opengl/OpenGLHelper.hxx>
#include <vcl/opengl/OpenGLContext.hxx>
#include <sal/log.hxx>

#include <algorithm>
#include <array>

#include <comphelper/random.hxx>

#include "Operation.hxx"
#include "TransitionImpl.hxx"
#include <cmath>

TransitionScene::TransitionScene(TransitionScene const& rOther)
    : maLeavingSlidePrimitives(rOther.maLeavingSlidePrimitives)
    , maEnteringSlidePrimitives(rOther.maEnteringSlidePrimitives)
    , maOverallOperations(rOther.maOverallOperations)
    , maSceneObjects(rOther.maSceneObjects)
{
}

TransitionScene& TransitionScene::operator=(const TransitionScene& rOther)
{
    TransitionScene aTmp(rOther);
    swap(aTmp);
    return *this;
}

void TransitionScene::swap(TransitionScene& rOther)
{
    using std::swap;

    swap(maLeavingSlidePrimitives, rOther.maLeavingSlidePrimitives);
    swap(maEnteringSlidePrimitives, rOther.maEnteringSlidePrimitives);
    swap(maOverallOperations, rOther.maOverallOperations);
    swap(maSceneObjects, rOther.maSceneObjects);
}

OGLTransitionImpl::~OGLTransitionImpl()
{
}

void OGLTransitionImpl::uploadModelViewProjectionMatrices()
{
    double EyePos(10.0);
    double const RealF(1.0);
    double const RealN(-1.0);
    double const RealL(-1.0);
    double RealR(1.0);
    double const RealB(-1.0);
    double RealT(1.0);
    double ClipN(EyePos+5.0*RealN);
    double ClipF(EyePos+15.0*RealF);
    double ClipL(RealL*8.0);
    double ClipR(RealR*8.0);
    double ClipB(RealB*8.0);
    double ClipT(RealT*8.0);

    glm::mat4 projection = glm::frustum<float>(ClipL, ClipR, ClipB, ClipT, ClipN, ClipF);
    //This scaling is to take the plane with BottomLeftCorner(-1,-1,0) and TopRightCorner(1,1,0) and map it to the screen after the perspective division.
    glm::vec3 scale(1.0 / (((RealR * 2.0 * ClipN) / (EyePos * (ClipR - ClipL))) - ((ClipR + ClipL) / (ClipR - ClipL))),
                    1.0 / (((RealT * 2.0 * ClipN) / (EyePos * (ClipT - ClipB))) - ((ClipT + ClipB) / (ClipT - ClipB))),
                    1.0);
    projection = glm::scale(projection, scale);
    glm::mat4 modelview = glm::translate(glm::mat4(), glm::vec3(0, 0, -EyePos));

    GLint location = glGetUniformLocation( m_nProgramObject, "u_projectionMatrix" );
    if( location != -1 ) {
        glUniformMatrix4fv(location, 1, false, glm::value_ptr(projection));
        CHECK_GL_ERROR();
    }

    location = glGetUniformLocation( m_nProgramObject, "u_modelViewMatrix" );
    if( location != -1 ) {
        glUniformMatrix4fv(location, 1, false, glm::value_ptr(modelview));
        CHECK_GL_ERROR();
    }
}

static std::vector<int> uploadPrimitives(const Primitives_t& primitives)
{
    int size = 0;
    for (const Primitive& primitive: primitives)
        size += primitive.getVerticesByteSize();

    CHECK_GL_ERROR();
    glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_STATIC_DRAW);
    CHECK_GL_ERROR();
    Vertex *buf = static_cast<Vertex*>(glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY));

    std::vector<int> indices;
    int last_pos = 0;
    for (const Primitive& primitive: primitives) {
        indices.push_back(last_pos);
        int num = primitive.writeVertices(buf);
        buf += num;
        last_pos += num;
    }

    CHECK_GL_ERROR();
    glUnmapBuffer(GL_ARRAY_BUFFER);
    CHECK_GL_ERROR();
    return indices;
}

bool OGLTransitionImpl::prepare( sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, OpenGLContext *pContext )
{
    m_nProgramObject = makeShader();
    if (!m_nProgramObject)
        return false;

    CHECK_GL_ERROR();
    glUseProgram( m_nProgramObject );
    CHECK_GL_ERROR();

    const SceneObjects_t& rSceneObjects(maScene.getSceneObjects());
    for(const auto& rSceneObject : rSceneObjects) {
        rSceneObject->prepare(m_nProgramObject);
    }

    GLint location = glGetUniformLocation( m_nProgramObject, "leavingSlideTexture" );
    if( location != -1 ) {
        glUniform1i( location, 0 );  // texture unit 0
        CHECK_GL_ERROR();
    }

    location = glGetUniformLocation( m_nProgramObject, "enteringSlideTexture" );
    if( location != -1 ) {
        glUniform1i( location, 2 );  // texture unit 2
        CHECK_GL_ERROR();
    }

    uploadModelViewProjectionMatrices();

    m_nPrimitiveTransformLocation = glGetUniformLocation( m_nProgramObject, "u_primitiveTransformMatrix" );
    m_nSceneTransformLocation = glGetUniformLocation( m_nProgramObject, "u_sceneTransformMatrix" );
    m_nOperationsTransformLocation = glGetUniformLocation( m_nProgramObject, "u_operationsTransformMatrix" );
    m_nTimeLocation = glGetUniformLocation( m_nProgramObject, "time" );

    glGenVertexArrays(1, &m_nVertexArrayObject);
    glBindVertexArray(m_nVertexArrayObject);

    glGenBuffers(1, &m_nVertexBufferObject);
    glBindBuffer(GL_ARRAY_BUFFER, m_nVertexBufferObject);

    // In practice both leaving and entering slides share the same primitives.
    m_nFirstIndices = uploadPrimitives(getScene().getLeavingSlide());

    // Attribute bindings
    m_nPositionLocation = glGetAttribLocation(m_nProgramObject, "a_position");
    if (m_nPositionLocation != -1) {
        glEnableVertexAttribArray(m_nPositionLocation);
        glVertexAttribPointer( m_nPositionLocation, 3, GL_FLOAT, false, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)) );
        CHECK_GL_ERROR();
    }

    m_nNormalLocation = glGetAttribLocation(m_nProgramObject, "a_normal");
    if (m_nNormalLocation != -1) {
        glEnableVertexAttribArray(m_nNormalLocation);
        glVertexAttribPointer( m_nNormalLocation, 3, GL_FLOAT, false, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)) );
        CHECK_GL_ERROR();
    }

    m_nTexCoordLocation = glGetAttribLocation(m_nProgramObject, "a_texCoord");
    if (m_nTexCoordLocation != -1) {
        glEnableVertexAttribArray(m_nTexCoordLocation);
        glVertexAttribPointer( m_nTexCoordLocation, 2, GL_FLOAT, false, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texcoord)) );
        CHECK_GL_ERROR();
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_GL_ERROR();

    prepareTransition( glLeavingSlideTex, glEnteringSlideTex, pContext );
    return true;
}

void OGLTransitionImpl::finish()
{
    const SceneObjects_t& rSceneObjects(maScene.getSceneObjects());
    for(const auto& rSceneObject : rSceneObjects) {
        rSceneObject->finish();
    }

    finishTransition();

    CHECK_GL_ERROR();
    if( m_nProgramObject ) {
        glDeleteBuffers(1, &m_nVertexBufferObject);
        m_nVertexBufferObject = 0;
        glDeleteVertexArrays(1, &m_nVertexArrayObject);
        m_nVertexArrayObject = 0;
        glDeleteProgram( m_nProgramObject );
        m_nProgramObject = 0;
    }
    CHECK_GL_ERROR();
}

void OGLTransitionImpl::prepare( double, double )
{
}

void OGLTransitionImpl::cleanup()
{
}

void OGLTransitionImpl::prepareTransition( sal_Int32, sal_Int32, OpenGLContext* )
{
}

void OGLTransitionImpl::finishTransition()
{
}

void OGLTransitionImpl::displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, double SlideWidthScale, double SlideHeightScale, OpenGLContext * )
{
    CHECK_GL_ERROR();
    applyOverallOperations( nTime, SlideWidthScale, SlideHeightScale );

    glUniform1f( m_nTimeLocation, nTime );

    glActiveTexture( GL_TEXTURE2 );
    glBindTexture( GL_TEXTURE_2D, glEnteringSlideTex );
    glActiveTexture( GL_TEXTURE0 );

    displaySlide( nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale );
    CHECK_GL_ERROR();
}

void OGLTransitionImpl::display( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex,
                                 double SlideWidth, double SlideHeight, double DispWidth, double DispHeight, OpenGLContext *pContext )
{
    const double SlideWidthScale = SlideWidth/DispWidth;
    const double SlideHeightScale = SlideHeight/DispHeight;

    CHECK_GL_ERROR();
    glBindVertexArray(m_nVertexArrayObject);
    prepare( SlideWidth, SlideHeight );

    CHECK_GL_ERROR();
    displaySlides_( nTime, glLeavingSlideTex, glEnteringSlideTex, SlideWidthScale, SlideHeightScale, pContext );
    CHECK_GL_ERROR();
    displayScene( nTime, SlideWidth, SlideHeight, DispWidth, DispHeight );
    CHECK_GL_ERROR();
}

void OGLTransitionImpl::applyOverallOperations( double nTime, double SlideWidthScale, double SlideHeightScale )
{
    const Operations_t& rOverallOperations(maScene.getOperations());
    glm::mat4 matrix;
    for(const auto& rOperation : rOverallOperations)
        rOperation->interpolate(matrix, nTime, SlideWidthScale, SlideHeightScale);
    CHECK_GL_ERROR();
    if (m_nOperationsTransformLocation != -1) {
        glUniformMatrix4fv(m_nOperationsTransformLocation, 1, false, glm::value_ptr(matrix));
        CHECK_GL_ERROR();
    }
}

static void displayPrimitives(const Primitives_t& primitives, GLint primitiveTransformLocation, double nTime, double WidthScale, double HeightScale, std::vector<int>::const_iterator first)
{
    for (const Primitive& primitive: primitives)
        primitive.display(primitiveTransformLocation, nTime, WidthScale, HeightScale, *first++);
}

void
OGLTransitionImpl::displaySlide(
        const double nTime,
        const sal_Int32 glSlideTex, const Primitives_t& primitives,
        double SlideWidthScale, double SlideHeightScale )
{
    CHECK_GL_ERROR();
    glBindTexture(GL_TEXTURE_2D, glSlideTex);
    CHECK_GL_ERROR();
    if (m_nSceneTransformLocation != -1) {
        glUniformMatrix4fv(m_nSceneTransformLocation, 1, false, glm::value_ptr(glm::mat4()));
        CHECK_GL_ERROR();
    }
    displayPrimitives(primitives, m_nPrimitiveTransformLocation, nTime, SlideWidthScale, SlideHeightScale, m_nFirstIndices.cbegin());
    CHECK_GL_ERROR();
}

void
OGLTransitionImpl::displayUnbufferedSlide(
        const double nTime,
        const sal_Int32 glSlideTex, const Primitives_t& primitives,
        double SlideWidthScale, double SlideHeightScale )
{
    CHECK_GL_ERROR();
    glBindTexture(GL_TEXTURE_2D, glSlideTex);
    CHECK_GL_ERROR();
    glBindVertexArray(0);
    CHECK_GL_ERROR();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_GL_ERROR();
    if (m_nSceneTransformLocation != -1) {
        glUniformMatrix4fv(m_nSceneTransformLocation, 1, false, glm::value_ptr(glm::mat4()));
        CHECK_GL_ERROR();
    }
    for (const Primitive& primitive: primitives)
        primitive.display(m_nPrimitiveTransformLocation, nTime, SlideWidthScale, SlideHeightScale);
    CHECK_GL_ERROR();
    glBindVertexArray(m_nVertexArrayObject);
    CHECK_GL_ERROR();
    glBindBuffer(GL_ARRAY_BUFFER, m_nVertexBufferObject);
    CHECK_GL_ERROR();
}

void OGLTransitionImpl::displayScene( double nTime, double SlideWidth, double SlideHeight, double DispWidth, double DispHeight )
{
    const SceneObjects_t& rSceneObjects(maScene.getSceneObjects());
    CHECK_GL_ERROR();
    for(const auto& rSceneObject : rSceneObjects)
        rSceneObject->display(m_nSceneTransformLocation, m_nPrimitiveTransformLocation, nTime, SlideWidth, SlideHeight, DispWidth, DispHeight);
    CHECK_GL_ERROR();
}

void Primitive::display(GLint primitiveTransformLocation, double nTime, double WidthScale, double HeightScale) const
{
    glm::mat4 matrix;
    applyOperations( matrix, nTime, WidthScale, HeightScale );

    CHECK_GL_ERROR();
    if (primitiveTransformLocation != -1) {
        glUniformMatrix4fv(primitiveTransformLocation, 1, false, glm::value_ptr(matrix));
        CHECK_GL_ERROR();
    }

    GLuint nVertexArrayObject;
    glGenVertexArrays(1, &nVertexArrayObject);
    CHECK_GL_ERROR();
    glBindVertexArray(nVertexArrayObject);
    CHECK_GL_ERROR();

    GLuint nBuffer;
    glGenBuffers(1, &nBuffer);
    CHECK_GL_ERROR();
    glBindBuffer(GL_ARRAY_BUFFER, nBuffer);
    CHECK_GL_ERROR();
    glBufferData(GL_ARRAY_BUFFER, getVerticesByteSize(), Vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    CHECK_GL_ERROR();
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    CHECK_GL_ERROR();
    glDrawArrays( GL_TRIANGLES, 0, Vertices.size() );
    CHECK_GL_ERROR();

    glDeleteBuffers(1, &nBuffer);
    CHECK_GL_ERROR();

    glDeleteVertexArrays(1, &nVertexArrayObject);
    CHECK_GL_ERROR();
}

void Primitive::display(GLint primitiveTransformLocation, double nTime, double WidthScale, double HeightScale, int first) const
{
    glm::mat4 matrix;
    applyOperations( matrix, nTime, WidthScale, HeightScale );

    CHECK_GL_ERROR();
    if (primitiveTransformLocation != -1) {
        glUniformMatrix4fv(primitiveTransformLocation, 1, false, glm::value_ptr(matrix));
        CHECK_GL_ERROR();
    }
    glDrawArrays( GL_TRIANGLES, first, Vertices.size() );

    CHECK_GL_ERROR();
}

void Primitive::applyOperations(glm::mat4& matrix, double nTime, double WidthScale, double HeightScale) const
{
    for(const auto & rOperation : Operations)
        rOperation->interpolate(matrix, nTime, WidthScale, HeightScale);
    matrix = glm::scale(matrix, glm::vec3(WidthScale, HeightScale, 1));
}

void SceneObject::display(GLint sceneTransformLocation, GLint primitiveTransformLocation, double nTime, double /* SlideWidth */, double /* SlideHeight */, double DispWidth, double DispHeight ) const
{
    // fixme: allow various model spaces, now we make it so that
    // it is regular -1,-1 to 1,1, where the whole display fits in
    glm::mat4 matrix;
    if (DispHeight > DispWidth)
        matrix = glm::scale(matrix, glm::vec3(DispHeight/DispWidth, 1, 1));
    else
        matrix = glm::scale(matrix, glm::vec3(1, DispWidth/DispHeight, 1));
    CHECK_GL_ERROR();
    if (sceneTransformLocation != -1) {
        glUniformMatrix4fv(sceneTransformLocation, 1, false, glm::value_ptr(matrix));
        CHECK_GL_ERROR();
    }
    displayPrimitives(maPrimitives, primitiveTransformLocation, nTime, 1, 1, maFirstIndices.cbegin());
    CHECK_GL_ERROR();
}

void SceneObject::pushPrimitive(const Primitive &p)
{
    maPrimitives.push_back(p);
}

SceneObject::SceneObject()
    : maPrimitives()
{
}

SceneObject::~SceneObject()
{
}

namespace
{

class Iris : public SceneObject
{
public:
    Iris() = default;

    virtual void prepare(GLuint program) override;
    virtual void display(GLint sceneTransformLocation, GLint primitiveTransformLocation, double nTime, double SlideWidth, double SlideHeight, double DispWidth, double DispHeight) const override;
    virtual void finish() override;

private:
    GLuint maTexture = 0;
    GLuint maBuffer = 0;
    GLuint maVertexArray = 0;
};

void Iris::display(GLint sceneTransformLocation, GLint primitiveTransformLocation, double nTime, double SlideWidth, double SlideHeight, double DispWidth, double DispHeight ) const
{
    glBindVertexArray(maVertexArray);
    CHECK_GL_ERROR();
    glBindTexture(GL_TEXTURE_2D, maTexture);
    CHECK_GL_ERROR();
    SceneObject::display(sceneTransformLocation, primitiveTransformLocation, nTime, SlideWidth, SlideHeight, DispWidth, DispHeight);
}

void Iris::prepare(GLuint program)
{
    CHECK_GL_ERROR();
    static const GLubyte img[3] = { 80, 80, 80 };

    glGenTextures(1, &maTexture);
    glBindTexture(GL_TEXTURE_2D, maTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    CHECK_GL_ERROR();

    glGenVertexArrays(1, &maVertexArray);
    glBindVertexArray(maVertexArray);

    glGenBuffers(1, &maBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, maBuffer);
    maFirstIndices = uploadPrimitives(maPrimitives);

    // Attribute bindings
    GLint location = glGetAttribLocation(program, "a_position");
    if (location != -1) {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer( location, 3, GL_FLOAT, false, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)) );
        CHECK_GL_ERROR();
    }

    location = glGetAttribLocation(program, "a_normal");
    if (location != -1) {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer( location, 3, GL_FLOAT, false, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)) );
        CHECK_GL_ERROR();
    }

    location = glGetAttribLocation(program, "a_texCoord");
    if (location != -1) {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer( location, 2, GL_FLOAT, false, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, texcoord)) );
        CHECK_GL_ERROR();
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Iris::finish()
{
    CHECK_GL_ERROR();
    glDeleteBuffers(1, &maBuffer);
    CHECK_GL_ERROR();
    glDeleteVertexArrays(1, &maVertexArray);
    CHECK_GL_ERROR();
    glDeleteTextures(1, &maTexture);
    CHECK_GL_ERROR();
}

}

namespace
{

class ReflectionTransition : public OGLTransitionImpl
{
public:
    ReflectionTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : OGLTransitionImpl(rScene, rSettings)
    {}

private:
    virtual GLuint makeShader() const override;
    virtual void displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, double SlideWidthScale, double SlideHeightScale, OpenGLContext *pContext ) override;

    virtual void prepareTransition( sal_Int32, sal_Int32, OpenGLContext* ) override {
        glDisable(GL_CULL_FACE);
    }

    virtual void finishTransition() override {
        glEnable(GL_CULL_FACE);
    }
};

GLuint ReflectionTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"reflectionVertexShader"_ustr, u"reflectionFragmentShader"_ustr );
}

void ReflectionTransition::displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex,
                                              double SlideWidthScale, double SlideHeightScale, OpenGLContext * )
{
    CHECK_GL_ERROR();
    applyOverallOperations( nTime, SlideWidthScale, SlideHeightScale );

    sal_Int32 texture;
    Primitives_t slide;
    if (nTime < 0.5) {
        texture = glLeavingSlideTex;
        slide = getScene().getLeavingSlide();
    } else {
        texture = glEnteringSlideTex;
        slide = getScene().getEnteringSlide();
    }

    displaySlide( nTime, texture, slide, SlideWidthScale, SlideHeightScale );
    CHECK_GL_ERROR();
}

std::shared_ptr<OGLTransitionImpl>
makeReflectionTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        Operations_t&& rOverallOperations,
        const TransitionSettings& rSettings)
{
    return std::make_shared<ReflectionTransition>(
            TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives), std::move(rOverallOperations), SceneObjects_t()),
            rSettings);
}

}

namespace
{

class SimpleTransition : public OGLTransitionImpl
{
public:
    SimpleTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : OGLTransitionImpl(rScene, rSettings)
    {
    }

private:
    virtual GLuint makeShader() const override;

    virtual void displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, double SlideWidthScale, double SlideHeightScale, OpenGLContext *pContext ) override;
};

GLuint SimpleTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"basicVertexShader"_ustr, u"basicFragmentShader"_ustr );
}

void SimpleTransition::displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex,
                                       double SlideWidthScale, double SlideHeightScale, OpenGLContext * )
{
    CHECK_GL_ERROR();
    applyOverallOperations( nTime, SlideWidthScale, SlideHeightScale );

    CHECK_GL_ERROR();
    displaySlide( nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale );
    displaySlide( nTime, glEnteringSlideTex, getScene().getEnteringSlide(), SlideWidthScale, SlideHeightScale );
    CHECK_GL_ERROR();
}

std::shared_ptr<OGLTransitionImpl>
makeSimpleTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        Operations_t&& rOverallOperations,
        SceneObjects_t&& rSceneObjects,
        const TransitionSettings& rSettings)
{
    return std::make_shared<SimpleTransition>(
            TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives),
                            std::move(rOverallOperations), std::move(rSceneObjects)),
            rSettings);
}

std::shared_ptr<OGLTransitionImpl>
makeSimpleTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        Operations_t&& rOverallOperations,
        const TransitionSettings& rSettings = TransitionSettings())
{
    return makeSimpleTransition(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives),
                                std::move(rOverallOperations), SceneObjects_t(), rSettings);
}

std::shared_ptr<OGLTransitionImpl>
makeSimpleTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        SceneObjects_t&& rSceneObjects,
        const TransitionSettings& rSettings)
{
    return makeSimpleTransition(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives),
                                Operations_t(), std::move(rSceneObjects), rSettings);
}

std::shared_ptr<OGLTransitionImpl>
makeSimpleTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        const TransitionSettings& rSettings = TransitionSettings())
{
    return makeSimpleTransition(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives),
                                Operations_t(), SceneObjects_t(), rSettings);
}

}

std::shared_ptr<OGLTransitionImpl> makeOutsideCubeFaceToLeft()
{
    Primitive Slide;

    Slide.pushTriangle(glm::vec2(0,0),glm::vec2(1,0),glm::vec2(0,1));
    Slide.pushTriangle(glm::vec2(1,0),glm::vec2(0,1),glm::vec2(1,1));

    Primitives_t aLeavingPrimitives;
    aLeavingPrimitives.push_back(Slide);

    Slide.Operations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0,1,0),glm::vec3(0,0,-1),90,false,false,0.0,1.0));

    Primitives_t aEnteringPrimitives;
    aEnteringPrimitives.push_back(Slide);

    Operations_t aOperations;
    aOperations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0,1,0),glm::vec3(0,0,-1),-90,false,true,0.0,1.0));

    return makeSimpleTransition(std::move(aLeavingPrimitives), std::move(aEnteringPrimitives), std::move(aOperations));
}

std::shared_ptr<OGLTransitionImpl> makeInsideCubeFaceToLeft()
{
    Primitive Slide;

    Slide.pushTriangle(glm::vec2(0,0),glm::vec2(1,0),glm::vec2(0,1));
    Slide.pushTriangle(glm::vec2(1,0),glm::vec2(0,1),glm::vec2(1,1));

    Primitives_t aLeavingPrimitives;
    aLeavingPrimitives.push_back(Slide);

    Slide.Operations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0,1,0),glm::vec3(0,0,1),-90,false,false,0.0,1.0));

    Primitives_t aEnteringPrimitives;
    aEnteringPrimitives.push_back(Slide);

    Operations_t aOperations;
    aOperations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0,1,0),glm::vec3(0,0,1),90,false,true,0.0,1.0));

    return makeSimpleTransition(std::move(aLeavingPrimitives), std::move(aEnteringPrimitives), std::move(aOperations));
}

std::shared_ptr<OGLTransitionImpl> makeFallLeaving()
{
    Primitive Slide;

    Slide.pushTriangle(glm::vec2(0,0),glm::vec2(1,0),glm::vec2(0,1));
    Slide.pushTriangle(glm::vec2(1,0),glm::vec2(0,1),glm::vec2(1,1));

    Primitives_t aEnteringPrimitives;
    aEnteringPrimitives.push_back(Slide);

    Slide.Operations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(1,0,0),glm::vec3(0,-1,0), 90,true,true,0.0,1.0));
    Primitives_t aLeavingPrimitives;
    aLeavingPrimitives.push_back(Slide);

    TransitionSettings aSettings;
    aSettings.mbUseMipMapEntering = false;

    return makeSimpleTransition(std::move(aLeavingPrimitives), std::move(aEnteringPrimitives), aSettings);
}

std::shared_ptr<OGLTransitionImpl> makeTurnAround()
{
    Primitive Slide;
    TransitionSettings aSettings;

    Slide.pushTriangle(glm::vec2(0,0),glm::vec2(1,0),glm::vec2(0,1));
    Slide.pushTriangle(glm::vec2(1,0),glm::vec2(0,1),glm::vec2(1,1));
    Primitives_t aLeavingPrimitives;
    aLeavingPrimitives.push_back(Slide);

    Slide.Operations.push_back(makeSScale(glm::vec3(1, -1, 1), glm::vec3(0, -1.02, 0), false, -1, 0));
    aLeavingPrimitives.push_back(Slide);

    Slide.Operations.clear();
    Slide.Operations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0,1,0),glm::vec3(0,0,0),-180,true,false,0.0,1.0));
    Primitives_t aEnteringPrimitives;
    aEnteringPrimitives.push_back(Slide);

    Slide.Operations.push_back(makeSScale(glm::vec3(1, -1, 1), glm::vec3(0, -1.02, 0), false, -1, 0));
    aEnteringPrimitives.push_back(Slide);

    Operations_t aOperations;
    aOperations.push_back(makeSTranslate(glm::vec3(0, 0, -1.5),true, 0, 0.5));
    aOperations.push_back(makeSTranslate(glm::vec3(0, 0, 1.5), true, 0.5, 1));
    aOperations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0, 1, 0),glm::vec3(0, 0, 0), -180, true, true, 0.0, 1.0));

    return makeReflectionTransition(std::move(aLeavingPrimitives), std::move(aEnteringPrimitives), std::move(aOperations), aSettings);
}

std::shared_ptr<OGLTransitionImpl> makeTurnDown()
{
    Primitive Slide;

    Slide.pushTriangle(glm::vec2(0,0),glm::vec2(1,0),glm::vec2(0,1));
    Slide.pushTriangle(glm::vec2(1,0),glm::vec2(0,1),glm::vec2(1,1));
    Primitives_t aLeavingPrimitives;
    aLeavingPrimitives.push_back(Slide);

    Slide.Operations.push_back(makeSTranslate(glm::vec3(0, 0, 0.0001), false, -1.0, 0.0));
    Slide.Operations.push_back(makeSRotate (glm::vec3(0, 0, 1), glm::vec3(-1, 1, 0), -90, true, 0.0, 1.0));
    Slide.Operations.push_back(makeSRotate (glm::vec3(0, 0, 1), glm::vec3(-1, 1, 0), 90, false, -1.0, 0.0));
    Primitives_t aEnteringPrimitives;
    aEnteringPrimitives.push_back(Slide);

    TransitionSettings aSettings;
    aSettings.mbUseMipMapLeaving = false;

    return makeSimpleTransition(std::move(aLeavingPrimitives), std::move(aEnteringPrimitives), aSettings);
}

std::shared_ptr<OGLTransitionImpl> makeIris()
{
    Primitive Slide;

    Slide.pushTriangle (glm::vec2 (0,0), glm::vec2 (1,0), glm::vec2 (0,1));
    Slide.pushTriangle (glm::vec2 (1,0), glm::vec2 (0,1), glm::vec2 (1,1));
    Primitives_t aEnteringPrimitives;
    aEnteringPrimitives.push_back (Slide);

    Slide.Operations.push_back (makeSTranslate (glm::vec3 (0, 0,  0.000001), false, -1, 0));
    Slide.Operations.push_back (makeSTranslate (glm::vec3 (0, 0, -0.000002), false, 0.5, 1));
    Primitives_t aLeavingPrimitives;
    aLeavingPrimitives.push_back (Slide);


    Primitive irisPart;
    int i, nSteps = 24, nParts = 7;
    double t = 1.0/nSteps, lx = 1, ly = 0, of=2.2, f=1.42;

    for (i=1; i<=nSteps; i++) {
        double x = cos ((3*2*M_PI*t)/nParts);
        double y = -sin ((3*2*M_PI*t)/nParts);
        double cx = (f*x + 1)/2;
        double cy = (f*y + 1)/2;
        double lcx = (f*lx + 1)/2;
        double lcy = (f*ly + 1)/2;
        double cxo = (of*x + 1)/2;
        double cyo = (of*y + 1)/2;
        double lcxo = (of*lx + 1)/2;
        double lcyo = (of*ly + 1)/2;
        irisPart.pushTriangle (glm::vec2 (lcx, lcy),
                               glm::vec2 (lcxo, lcyo),
                               glm::vec2 (cx, cy));
        irisPart.pushTriangle (glm::vec2 (cx, cy),
                               glm::vec2 (lcxo, lcyo),
                               glm::vec2 (cxo, cyo));
        lx = x;
        ly = y;
        t += 1.0/nSteps;
    }

    std::shared_ptr<Iris> pIris = std::make_shared<Iris>();
    double angle = 87;

    for (i = 0; i < nParts; i++) {
        irisPart.Operations.clear ();
        double rx, ry;

        rx = cos ((2*M_PI*i)/nParts);
        ry = sin ((2*M_PI*i)/nParts);
        irisPart.Operations.push_back (makeSRotate (glm::vec3(0, 0, 1), glm::vec3(rx, ry, 0),  angle, true, 0.0, 0.5));
        irisPart.Operations.push_back (makeSRotate (glm::vec3(0, 0, 1), glm::vec3(rx, ry, 0), -angle, true, 0.5, 1));
        if (i > 0) {
            irisPart.Operations.push_back (makeSTranslate (glm::vec3(rx, ry, 0),  false, -1, 0));
            irisPart.Operations.push_back (makeSRotate (glm::vec3(0, 0, 1), glm::vec3(0, 0, 0), i*360.0/nParts, false, -1, 0));
            irisPart.Operations.push_back (makeSTranslate (glm::vec3(-1, 0, 0),  false, -1, 0));
        }
        irisPart.Operations.push_back(makeSTranslate(glm::vec3(0, 0, 1), false, -2, 0.0));
        irisPart.Operations.push_back (makeSRotate (glm::vec3(1, .5, 0), glm::vec3(1, 0, 0), -30, false, -1, 0));
        pIris->pushPrimitive (irisPart);
    }

    SceneObjects_t aSceneObjects;
    aSceneObjects.push_back (pIris);

    TransitionSettings aSettings;
    aSettings.mbUseMipMapLeaving = aSettings.mbUseMipMapEntering = false;

    return makeSimpleTransition(std::move(aLeavingPrimitives), std::move(aEnteringPrimitives), std::move(aSceneObjects), aSettings);
}

namespace
{

class RochadeTransition : public ReflectionTransition
{
public:
    RochadeTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : ReflectionTransition(rScene, rSettings)
    {}

private:
    virtual void displaySlides_(double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, double SlideWidthScale, double SlideHeightScale, OpenGLContext *pContext) override;
};

void RochadeTransition::displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, double SlideWidthScale, double SlideHeightScale, OpenGLContext * )
{
    applyOverallOperations( nTime, SlideWidthScale, SlideHeightScale );

    if( nTime > .5) {
        displaySlide( nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale );
        displaySlide( nTime, glEnteringSlideTex, getScene().getEnteringSlide(), SlideWidthScale, SlideHeightScale );
    } else {
        displaySlide( nTime, glEnteringSlideTex, getScene().getEnteringSlide(), SlideWidthScale, SlideHeightScale );
        displaySlide( nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale );
    }
}

std::shared_ptr<OGLTransitionImpl>
makeRochadeTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        const TransitionSettings& rSettings)
{
    return std::make_shared<RochadeTransition>(
            TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives)),
            rSettings)
        ;

}
}

std::shared_ptr<OGLTransitionImpl> makeRochade()
{
    Primitive Slide;
    TransitionSettings aSettings;

    double w, h;

    w = 2.2;
    h = 10;

    Slide.pushTriangle(glm::vec2(0,0),glm::vec2(1,0),glm::vec2(0,1));
    Slide.pushTriangle(glm::vec2(1,0),glm::vec2(0,1),glm::vec2(1,1));

    Slide.Operations.push_back(makeSEllipseTranslate(w, h, 0.25, -0.25, true, 0, 1));
    Slide.Operations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0,1,0),glm::vec3(0,0,0), -45, true, true, 0, 1));
    Primitives_t aLeavingSlide;
    aLeavingSlide.push_back(Slide);

    Slide.Operations.push_back(makeSScale(glm::vec3(1, -1, 1), glm::vec3(0, -1.02, 0), false, -1, 0));
    aLeavingSlide.push_back(Slide);

    Slide.Operations.clear();
    Slide.Operations.push_back(makeSEllipseTranslate(w, h, 0.75, 0.25, true, 0, 1));
    Slide.Operations.push_back(makeSTranslate(glm::vec3(0, 0, -h), false, -1, 0));
    Slide.Operations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0,1,0),glm::vec3(0,0,0), -45, true, true, 0, 1));
    Slide.Operations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0,1,0),glm::vec3(0,0,0), 45, true, false, -1, 0));
    Primitives_t aEnteringSlide;
    aEnteringSlide.push_back(Slide);

    Slide.Operations.push_back(makeSScale(glm::vec3(1, -1, 1), glm::vec3(0, -1.02, 0), false, -1, 0));
    aEnteringSlide.push_back(Slide);

    return makeRochadeTransition(std::move(aLeavingSlide), std::move(aEnteringSlide), aSettings);
}

static double randFromNeg1to1()
{
    return comphelper::rng::uniform_real_distribution(-1.0, std::nextafter(1.0, DBL_MAX));
}

// TODO(Q3): extract to basegfx
static glm::vec3 randNormVectorInXYPlane()
{
    glm::vec3 toReturn(randFromNeg1to1(),randFromNeg1to1(),0.0);
    return glm::normalize(toReturn);
}

template<typename T>
static T clamp(const T& rIn)
{
    return glm::clamp(rIn, T(-1.0), T(1.0));
}

std::shared_ptr<OGLTransitionImpl> makeRevolvingCircles( sal_uInt16 nCircles , sal_uInt16 nPointsOnCircles )
{
    double dAngle(2*M_PI/static_cast<double>( nPointsOnCircles ));
    if(nCircles < 2 || nPointsOnCircles < 4)
        return makeNByMTileFlip(1,1);
    float Radius(1.0/static_cast<double>( nCircles ));
    float dRadius(Radius);
    float LastRadius(0.0);
    float NextRadius(2*Radius);

    /// now we know there is at least two circles
    /// the first will always be a full circle
    /// the last will always be the outer shell of the slide with a circle hole

    //add the full circle
    std::vector<glm::vec2> unScaledTexCoords;
    float TempAngle(0.0);
    for(unsigned int Point(0); Point < nPointsOnCircles; ++Point)
    {
        unScaledTexCoords.emplace_back( cos(TempAngle - M_PI_2) , sin(TempAngle- M_PI_2) );

        TempAngle += dAngle;
    }

    Primitives_t aLeavingSlide;
    Primitives_t aEnteringSlide;
    {
        Primitive EnteringSlide;
        Primitive LeavingSlide;
        for(int Point(0); Point + 1 < nPointsOnCircles; ++Point)
        {
            EnteringSlide.pushTriangle( glm::vec2( 0.5 , 0.5 ) , Radius * unScaledTexCoords[ Point + 1 ] / 2.0f + glm::vec2( 0.5 , 0.5 ) , Radius * unScaledTexCoords[ Point ] / 2.0f + glm::vec2( 0.5 , 0.5 ) );
            LeavingSlide.pushTriangle( glm::vec2( 0.5 , 0.5 ) , Radius * unScaledTexCoords[ Point + 1 ] / 2.0f + glm::vec2( 0.5 , 0.5 ) , Radius * unScaledTexCoords[ Point ] / 2.0f + glm::vec2( 0.5, 0.5) );
        }
        EnteringSlide.pushTriangle( glm::vec2(0.5,0.5) , Radius * unScaledTexCoords[ 0 ] / 2.0f + glm::vec2( 0.5 , 0.5 ) , Radius * unScaledTexCoords[ nPointsOnCircles - 1 ] / 2.0f + glm::vec2( 0.5 , 0.5 ) );
        LeavingSlide.pushTriangle( glm::vec2(0.5,0.5) , Radius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) , Radius*unScaledTexCoords[nPointsOnCircles - 1]/2.0f + glm::vec2(0.5,0.5) );

        glm::vec3 axis(randNormVectorInXYPlane());
        EnteringSlide.Operations.push_back( makeSRotate( axis , glm::vec3(0,0,0) , 180, true, Radius/2.0 , (NextRadius + 1)/2.0 ) );
        LeavingSlide.Operations.push_back( makeSRotate( axis , glm::vec3(0,0,0) , 180, true, Radius/2.0 , (NextRadius + 1)/2.0 ) );
        EnteringSlide.Operations.push_back( makeSRotate( axis , glm::vec3(0,0,0) , -180, false,0.0,1.0) );

        aEnteringSlide.push_back(EnteringSlide);
        aLeavingSlide.push_back(LeavingSlide);
        LastRadius = Radius;
        Radius = NextRadius;
        NextRadius += dRadius;
    }

    for(int i(1); i < nCircles - 1; ++i)
    {
        Primitive LeavingSlide;
        Primitive EnteringSlide;
        for(int Side(0); Side < nPointsOnCircles - 1; ++Side)
        {
            EnteringSlide.pushTriangle(Radius*unScaledTexCoords[Side]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) );
            EnteringSlide.pushTriangle(Radius*unScaledTexCoords[Side]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) , Radius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) );

            LeavingSlide.pushTriangle(Radius*unScaledTexCoords[Side]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) );
            LeavingSlide.pushTriangle(Radius*unScaledTexCoords[Side]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) , Radius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) );
        }

        EnteringSlide.pushTriangle(Radius*unScaledTexCoords[nPointsOnCircles - 1]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[nPointsOnCircles - 1]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) );
        EnteringSlide.pushTriangle(Radius*unScaledTexCoords[nPointsOnCircles - 1]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) , Radius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) );

        LeavingSlide.pushTriangle(Radius*unScaledTexCoords[nPointsOnCircles - 1]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[nPointsOnCircles - 1]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) );
        LeavingSlide.pushTriangle(Radius*unScaledTexCoords[nPointsOnCircles - 1]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) , Radius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) );

        glm::vec3 axis(randNormVectorInXYPlane());
        EnteringSlide.Operations.push_back( makeSRotate( axis , glm::vec3(0,0,0) , 180, true, Radius/2.0 , (NextRadius + 1)/2.0 ) );
        LeavingSlide.Operations.push_back( makeSRotate( axis , glm::vec3(0,0,0) , 180, true, Radius/2.0 , (NextRadius + 1)/2.0 ) );
        EnteringSlide.Operations.push_back( makeSRotate( axis , glm::vec3(0,0,0) , -180, false,0.0,1.0) );

        aEnteringSlide.push_back(EnteringSlide);
        aLeavingSlide.push_back(LeavingSlide);

        LastRadius = Radius;
        Radius = NextRadius;
        NextRadius += dRadius;
    }
    {
        Radius = sqrt(2.0);
        Primitive LeavingSlide;
        Primitive EnteringSlide;
        for(int Side(0); Side < nPointsOnCircles - 1; ++Side)
        {

            EnteringSlide.pushTriangle(clamp(Radius*unScaledTexCoords[Side])/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) );
            EnteringSlide.pushTriangle(clamp(Radius*unScaledTexCoords[Side])/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) , clamp(Radius*unScaledTexCoords[Side + 1])/2.0f + glm::vec2(0.5,0.5) );

            LeavingSlide.pushTriangle(clamp(Radius*unScaledTexCoords[Side])/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) );
            LeavingSlide.pushTriangle(clamp(Radius*unScaledTexCoords[Side])/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[Side + 1]/2.0f + glm::vec2(0.5,0.5) , clamp(Radius*unScaledTexCoords[Side + 1])/2.0f + glm::vec2(0.5,0.5) );
        }

        EnteringSlide.pushTriangle(clamp(Radius*unScaledTexCoords[nPointsOnCircles - 1])/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[nPointsOnCircles - 1]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) );
        EnteringSlide.pushTriangle(clamp(Radius*unScaledTexCoords[nPointsOnCircles - 1])/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) , clamp(Radius*unScaledTexCoords[0])/2.0f + glm::vec2(0.5,0.5) );

        LeavingSlide.pushTriangle(clamp(Radius*unScaledTexCoords[nPointsOnCircles - 1])/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[nPointsOnCircles - 1]/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) );
        LeavingSlide.pushTriangle(clamp(Radius*unScaledTexCoords[nPointsOnCircles - 1])/2.0f + glm::vec2(0.5,0.5) , LastRadius*unScaledTexCoords[0]/2.0f + glm::vec2(0.5,0.5) , clamp(Radius*unScaledTexCoords[0])/2.0f + glm::vec2(0.5,0.5) );

        glm::vec3 axis(randNormVectorInXYPlane());
        EnteringSlide.Operations.push_back( makeSRotate( axis , glm::vec3(0,0,0) , 180, true, (LastRadius + dRadius)/2.0 , 1.0 ) );
        LeavingSlide.Operations.push_back( makeSRotate( axis , glm::vec3(0,0,0) , 180, true, (LastRadius + dRadius)/2.0 , 1.0 ) );
        EnteringSlide.Operations.push_back( makeSRotate( axis , glm::vec3(0,0,0) , -180, false,0.0,1.0) );

        aEnteringSlide.push_back(EnteringSlide);
        aLeavingSlide.push_back(LeavingSlide);
    }

    return makeSimpleTransition(std::move(aLeavingSlide), std::move(aEnteringSlide));
}

std::shared_ptr<OGLTransitionImpl> makeHelix( sal_uInt16 nRows )
{
    double invN(1.0/static_cast<double>(nRows));
    double iDn = 0.0;
    double iPDn = invN;
    Primitives_t aLeavingSlide;
    Primitives_t aEnteringSlide;
    for(unsigned int i(0); i < nRows; ++i)
    {
        Primitive Tile;

        Tile.pushTriangle(glm::vec2( 1.0 , iDn ) , glm::vec2( 0.0 , iDn ) , glm::vec2( 0.0 , iPDn ));

        Tile.pushTriangle(glm::vec2( 1.0 , iPDn ) , glm::vec2( 1.0 , iDn ) , glm::vec2( 0.0 , iPDn ));

        Tile.Operations.push_back( makeSRotate( glm::vec3( 0 , 1 , 0 ) , ( Tile.getVertex(1) + Tile.getVertex(3) )/2.0f , 180 ,
                                                true, std::min(std::max(static_cast<double>(i - nRows/2.0)*invN/2.0,0.0),1.0),
                                                std::min(std::max(static_cast<double>(i + nRows/2.0)*invN/2.0,0.0),1.0) ) );

        aLeavingSlide.push_back(Tile);

        Tile.Operations.push_back( makeSRotate( glm::vec3( 0 , 1 , 0 ) , ( Tile.getVertex(1) + Tile.getVertex(3) )/2.0f , -180 , false,0.0,1.0) );

        aEnteringSlide.push_back(Tile);

        iDn += invN;
        iPDn += invN;
    }

    return makeSimpleTransition(std::move(aLeavingSlide), std::move(aEnteringSlide));
}

static float fdiv(int a, int b)
{
    return static_cast<float>(a)/b;
}

static glm::vec2 vec(float x, float y, float nx, float ny)
{
    x = x < 0.0 ? 0.0 : x;
    x = std::min(x, nx);
    y = y < 0.0 ? 0.0 : y;
    y = std::min(y, ny);
    return glm::vec2(fdiv(x, nx), fdiv(y, ny));
}

std::shared_ptr<OGLTransitionImpl> makeNByMTileFlip( sal_uInt16 n, sal_uInt16 m )
{
    Primitives_t aLeavingSlide;
    Primitives_t aEnteringSlide;

    for (int x = 0; x < n; x++)
    {
        for (int y = 0; y < n; y++)
        {
            Primitive aTile;
            glm::vec2 x11 = vec(x,   y,   n, m);
            glm::vec2 x12 = vec(x,   y+1, n, m);
            glm::vec2 x21 = vec(x+1, y,   n, m);
            glm::vec2 x22 = vec(x+1, y+1, n, m);

            aTile.pushTriangle(x21, x11, x12);
            aTile.pushTriangle(x22, x21, x12);

            aTile.Operations.push_back(makeSRotate( glm::vec3(0 , 1, 0), (aTile.getVertex(1) + aTile.getVertex(3)) / 2.0f, 180 , true, x11.x * x11.y / 2.0f , ((x22.x * x22.y) + 1.0f) / 2.0f));
            aLeavingSlide.push_back(aTile);

            aTile.Operations.push_back(makeSRotate( glm::vec3(0 , 1, 0), (aTile.getVertex(1) + aTile.getVertex(3)) / 2.0f, -180, false, x11.x * x11.y / 2.0f , ((x22.x * x22.y) + 1.0f) / 2.0f));
            aEnteringSlide.push_back(aTile);
        }
    }

    return makeSimpleTransition(std::move(aLeavingSlide), std::move(aEnteringSlide));
}

Primitive& Primitive::operator=(const Primitive& rvalue)
{
    Primitive aTmp(rvalue);
    swap(aTmp);
    return *this;
}

Primitive::Primitive(const Primitive& rvalue)
    : Operations(rvalue.Operations)
    , Vertices(rvalue.Vertices)
{
}

void Primitive::swap(Primitive& rOther)
{
    using std::swap;

    swap(Operations, rOther.Operations);
    swap(Vertices, rOther.Vertices);
}

void Primitive::pushTriangle(const glm::vec2& SlideLocation0,const glm::vec2& SlideLocation1,const glm::vec2& SlideLocation2)
{
    std::vector<glm::vec3> Verts;
    std::vector<glm::vec2> Texs;
    Verts.reserve(3);
    Texs.reserve(3);

    Verts.emplace_back( 2*SlideLocation0.x - 1, -2*SlideLocation0.y + 1 , 0.0 );
    Verts.emplace_back( 2*SlideLocation1.x - 1, -2*SlideLocation1.y + 1 , 0.0 );
    Verts.emplace_back( 2*SlideLocation2.x - 1, -2*SlideLocation2.y + 1 , 0.0 );

    //figure out if they're facing the correct way, and make them face the correct way.
    glm::vec3 Normal( glm::cross( Verts[0] - Verts[1] , Verts[1] - Verts[2] ) );
    if(Normal.z >= 0.0)//if the normal is facing us
    {
        Texs.push_back(SlideLocation0);
        Texs.push_back(SlideLocation1);
        Texs.push_back(SlideLocation2);
    }
    else // if the normal is facing away from us, make it face us
    {
        Texs.push_back(SlideLocation0);
        Texs.push_back(SlideLocation2);
        Texs.push_back(SlideLocation1);
        Verts.clear();
        Verts.emplace_back( 2*SlideLocation0.x - 1, -2*SlideLocation0.y + 1 , 0.0 );
        Verts.emplace_back( 2*SlideLocation2.x - 1, -2*SlideLocation2.y + 1 , 0.0 );
        Verts.emplace_back( 2*SlideLocation1.x - 1, -2*SlideLocation1.y + 1 , 0.0 );
    }

    Vertices.push_back({Verts[0], glm::vec3(0, 0, 1), Texs[0]}); //all normals always face the screen when untransformed.
    Vertices.push_back({Verts[1], glm::vec3(0, 0, 1), Texs[1]}); //all normals always face the screen when untransformed.
    Vertices.push_back({Verts[2], glm::vec3(0, 0, 1), Texs[2]}); //all normals always face the screen when untransformed.
}

namespace
{

class DiamondTransition : public SimpleTransition
{
public:
    DiamondTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : SimpleTransition(rScene, rSettings)
        {}

private:
    virtual void displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, double SlideWidthScale, double SlideHeightScale, OpenGLContext *pContext ) override;
};

Primitives_t makeLeavingSlide(double nTime)
{
    Primitive Slide2;
    if( nTime >= 0.5 ) {
        double m = 1 - nTime;

        Slide2.pushTriangle (glm::vec2 (0,0), glm::vec2 (m,0), glm::vec2 (0,m));
        Slide2.pushTriangle (glm::vec2 (nTime,0), glm::vec2 (1,0), glm::vec2 (1,m));
        Slide2.pushTriangle (glm::vec2 (1,nTime), glm::vec2 (1,1), glm::vec2 (nTime,1));
        Slide2.pushTriangle (glm::vec2 (0,nTime), glm::vec2 (m,1), glm::vec2 (0,1));
    } else {
        double l = 0.5 - nTime;
        double h = 0.5 + nTime;

        Slide2.pushTriangle (glm::vec2 (0,0), glm::vec2 (1,0), glm::vec2 (0.5,l));
        Slide2.pushTriangle (glm::vec2 (0.5,l), glm::vec2 (1,0), glm::vec2 (h,0.5));
        Slide2.pushTriangle (glm::vec2 (1,0), glm::vec2 (1,1), glm::vec2 (h,0.5));
        Slide2.pushTriangle (glm::vec2 (h,0.5), glm::vec2 (1,1), glm::vec2 (0.5,h));
        Slide2.pushTriangle (glm::vec2 (0.5,h), glm::vec2 (1,1), glm::vec2 (0,1));
        Slide2.pushTriangle (glm::vec2 (l,0.5), glm::vec2 (0.5,h), glm::vec2 (0,1));
        Slide2.pushTriangle (glm::vec2 (0,0), glm::vec2 (l,0.5), glm::vec2 (0,1));
        Slide2.pushTriangle (glm::vec2 (0,0), glm::vec2 (0.5,l), glm::vec2 (l,0.5));
    }
    Slide2.Operations.push_back (makeSTranslate (glm::vec3 (0, 0, 0.00000001), false, -1, 0));
    Primitives_t aLeavingSlidePrimitives;
    aLeavingSlidePrimitives.push_back (Slide2);

    return aLeavingSlidePrimitives;
}

void DiamondTransition::displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex,
                                        double SlideWidthScale, double SlideHeightScale, OpenGLContext * )
{
    CHECK_GL_ERROR();
    applyOverallOperations( nTime, SlideWidthScale, SlideHeightScale );

    CHECK_GL_ERROR();
    displayUnbufferedSlide( nTime, glLeavingSlideTex, makeLeavingSlide(nTime), SlideWidthScale, SlideHeightScale );
    displaySlide( nTime, glEnteringSlideTex, getScene().getEnteringSlide(), SlideWidthScale, SlideHeightScale );
    CHECK_GL_ERROR();
}

std::shared_ptr<OGLTransitionImpl>
makeDiamondTransition(const TransitionSettings& rSettings)
{
    Primitive Slide1;
    Slide1.pushTriangle (glm::vec2 (0,0), glm::vec2 (1,0), glm::vec2 (0,1));
    Slide1.pushTriangle (glm::vec2 (1,0), glm::vec2 (0,1), glm::vec2 (1,1));
    Primitives_t aEnteringSlidePrimitives;
    aEnteringSlidePrimitives.push_back (Slide1);
    Primitives_t aLeavingSlidePrimitives;
    aLeavingSlidePrimitives.push_back (Slide1);
    return std::make_shared<DiamondTransition>(TransitionScene(std::move(aLeavingSlidePrimitives), std::move(aEnteringSlidePrimitives)), rSettings);
}

}

std::shared_ptr<OGLTransitionImpl> makeDiamond()
{
    TransitionSettings aSettings;
    aSettings.mbUseMipMapLeaving = aSettings.mbUseMipMapEntering = false;

    return makeDiamondTransition(aSettings);
}

std::shared_ptr<OGLTransitionImpl> makeVenetianBlinds( bool vertical, int parts )
{
    static double t30 = tan( M_PI/6.0 );
    double ln = 0;
    double p = 1.0/parts;

    Primitives_t aLeavingSlide;
    Primitives_t aEnteringSlide;
    for( int i=0; i<parts; i++ ) {
        Primitive Slide;
        double n = (i + 1)/static_cast<double>(parts);
        if( vertical ) {
            Slide.pushTriangle (glm::vec2 (ln,0), glm::vec2 (n,0), glm::vec2 (ln,1));
            Slide.pushTriangle (glm::vec2 (n,0), glm::vec2 (ln,1), glm::vec2 (n,1));
            Slide.Operations.push_back(makeRotateAndScaleDepthByWidth(glm::vec3(0, 1, 0), glm::vec3(n + ln - 1, 0, -t30*p), -120, true, true, 0.0, 1.0));
        } else {
            Slide.pushTriangle (glm::vec2 (0,ln), glm::vec2 (1,ln), glm::vec2 (0,n));
            Slide.pushTriangle (glm::vec2 (1,ln), glm::vec2 (0,n), glm::vec2 (1,n));
            Slide.Operations.push_back(makeRotateAndScaleDepthByHeight(glm::vec3(1, 0, 0), glm::vec3(0, 1 - n - ln, -t30*p), -120, true, true, 0.0, 1.0));
        }
        aLeavingSlide.push_back (Slide);

        if( vertical ) {
            Slide.Operations.push_back(makeSRotate(glm::vec3(0, 1, 0), glm::vec3(2*n - 1, 0, 0), -60, false, -1, 0));
            Slide.Operations.push_back(makeSRotate(glm::vec3(0, 1, 0), glm::vec3(n + ln - 1, 0, 0), 180, false, -1, 0));
        } else {
            Slide.Operations.push_back(makeSRotate(glm::vec3(1, 0, 0), glm::vec3(0, 1 - 2*n, 0), -60, false, -1, 0));
            Slide.Operations.push_back(makeSRotate(glm::vec3(1, 0, 0), glm::vec3(0, 1 - n - ln, 0), 180, false, -1, 0));
        }
        aEnteringSlide.push_back (Slide);
        ln = n;
    }

    return makeSimpleTransition(std::move(aLeavingSlide), std::move(aEnteringSlide));
}

namespace
{

class FadeSmoothlyTransition : public OGLTransitionImpl
{
public:
    FadeSmoothlyTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : OGLTransitionImpl(rScene, rSettings)
    {}

private:
    virtual GLuint makeShader() const override;
};

GLuint FadeSmoothlyTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"basicVertexShader"_ustr, u"fadeFragmentShader"_ustr );
}

std::shared_ptr<OGLTransitionImpl>
makeFadeSmoothlyTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        const TransitionSettings& rSettings)
{
    return std::make_shared<FadeSmoothlyTransition>(
            TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives)),
            rSettings)
        ;
}

}

std::shared_ptr<OGLTransitionImpl> makeFadeSmoothly()
{
    Primitive Slide;

    Slide.pushTriangle (glm::vec2 (0,0), glm::vec2 (1,0), glm::vec2 (0,1));
    Slide.pushTriangle (glm::vec2 (1,0), glm::vec2 (0,1), glm::vec2 (1,1));
    Primitives_t aLeavingSlide;
    aLeavingSlide.push_back (Slide);
    Primitives_t aEnteringSlide;
    aEnteringSlide.push_back (Slide);

    TransitionSettings aSettings;
    aSettings.mbUseMipMapLeaving = aSettings.mbUseMipMapEntering = false;

    return makeFadeSmoothlyTransition(std::move(aLeavingSlide), std::move(aEnteringSlide), aSettings);
}

namespace
{

class FadeThroughColorTransition : public OGLTransitionImpl
{
public:
    FadeThroughColorTransition(const TransitionScene& rScene, const TransitionSettings& rSettings, bool white)
        : OGLTransitionImpl(rScene, rSettings), useWhite( white )
    {}

private:
    virtual GLuint makeShader() const override;
    bool useWhite;
};

GLuint FadeThroughColorTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"basicVertexShader"_ustr, u"fadeBlackFragmentShader"_ustr,
        useWhite ? "#define use_white" : "", "" );
}

std::shared_ptr<OGLTransitionImpl>
makeFadeThroughColorTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        const TransitionSettings& rSettings,
        bool white)
{
    return std::make_shared<FadeThroughColorTransition>(
            TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives)),
            rSettings, white)
        ;
}

}

std::shared_ptr<OGLTransitionImpl> makeFadeThroughColor( bool white )
{
    Primitive Slide;

    Slide.pushTriangle (glm::vec2 (0,0), glm::vec2 (1,0), glm::vec2 (0,1));
    Slide.pushTriangle (glm::vec2 (1,0), glm::vec2 (0,1), glm::vec2 (1,1));
    Primitives_t aLeavingSlide;
    aLeavingSlide.push_back (Slide);
    Primitives_t aEnteringSlide;
    aEnteringSlide.push_back (Slide);

    TransitionSettings aSettings;
    aSettings.mbUseMipMapLeaving = aSettings.mbUseMipMapEntering = false;

    return makeFadeThroughColorTransition(std::move(aLeavingSlide), std::move(aEnteringSlide), aSettings, white);
}

namespace
{

class PermTextureTransition : public OGLTransitionImpl
{
protected:
    PermTextureTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : OGLTransitionImpl(rScene, rSettings)
        , m_nHelperTexture(0)
    {}

    virtual void finishTransition() override;
    virtual void prepareTransition( sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, OpenGLContext *pContext ) override;

private:
    /** various data */
    GLuint m_nHelperTexture;
};

void PermTextureTransition::finishTransition()
{
    CHECK_GL_ERROR();
    if ( m_nHelperTexture )
    {
        glDeleteTextures( 1, &m_nHelperTexture );
        m_nHelperTexture = 0;
    }
    CHECK_GL_ERROR();
}

constexpr auto permutation2D = []() constexpr {
    int permutation256 [256]= {
    215, 100, 200, 204, 233,  50,  85, 196,
     71, 141, 122, 160,  93, 131, 243, 234,
    162, 183,  36, 155,   4,  62,  35, 205,
     40, 102,  33,  27, 255,  55, 214, 156,
     75, 163, 134, 126, 249,  74, 197, 228,
     72,  90, 206, 235,  17,  22,  49, 169,
    227,  89,  16,   5, 117,  60, 248, 230,
    217,  68, 138,  96, 194, 170, 136,  10,
    112, 238, 184, 189, 176,  42, 225, 212,
     84,  58, 175, 244, 150, 168, 219, 236,
    101, 208, 123,  37, 164, 110, 158, 201,
     78, 114,  57,  48,  70, 142, 106,  43,
    232,  26,  32, 252, 239,  98, 191,  94,
     59, 149,  39, 187, 203, 190,  19,  13,
    133,  45,  61, 247,  23,  34,  20,  52,
    118, 209, 146, 193, 222,  18,   1, 152,
     46,  41,  91, 148, 115,  25, 135,  77,
    254, 147, 224, 161,   9, 213, 223, 250,
    231, 251, 127, 166,  63, 179,  81, 130,
    139,  28, 120, 151, 241,  86, 111,   0,
     88, 153, 172, 182, 159, 105, 178,  47,
     51, 167,  65,  66,  92,  73, 198, 211,
    245, 195,  31, 220, 140,  76, 221, 186,
    154, 185,  56,  83,  38, 165, 109,  67,
    124, 226, 132,  53, 229,  29,  12, 181,
    121,  24, 207, 199, 177, 113,  30,  80,
      3,  97, 188,  79, 216, 173,   8, 145,
     87, 128, 180, 237, 240, 137, 125, 104,
     15, 242, 119, 246, 103, 143,  95, 144,
      2,  44,  69, 157, 192, 174,  14,  54,
    218,  82,  64, 210,  11,   6, 129,  21,
    116, 171,  99, 202,   7, 107, 253, 108
    };
    std::array<unsigned char, 256 * 256> a{};
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            a[x + y * 256] = permutation256[(y + permutation256[x]) & 0xff];
    return a;
}();

void initPermTexture(GLuint *texID)
{
    CHECK_GL_ERROR();
    glGenTextures(1, texID);
    glBindTexture(GL_TEXTURE_2D, *texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RED, GL_UNSIGNED_BYTE,
                 permutation2D.data());
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    CHECK_GL_ERROR();
}

void PermTextureTransition::prepareTransition( sal_Int32, sal_Int32, OpenGLContext* )
{
    CHECK_GL_ERROR();
    GLint location = glGetUniformLocation( m_nProgramObject, "permTexture" );
    if( location != -1 ) {
        glActiveTexture(GL_TEXTURE1);
        CHECK_GL_ERROR();
        if( !m_nHelperTexture )
            initPermTexture( &m_nHelperTexture );

        glActiveTexture(GL_TEXTURE0);
        CHECK_GL_ERROR();

        glUniform1i( location, 1 );  // texture unit 1
        CHECK_GL_ERROR();
    }
    CHECK_GL_ERROR();
}

}

namespace
{

class StaticNoiseTransition : public PermTextureTransition
{
public:
    StaticNoiseTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : PermTextureTransition(rScene, rSettings)
    {}

private:
    virtual GLuint makeShader() const override;
};

GLuint StaticNoiseTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"basicVertexShader"_ustr, u"staticFragmentShader"_ustr );
}

std::shared_ptr<OGLTransitionImpl>
makeStaticNoiseTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        const TransitionSettings& rSettings)
{
    return std::make_shared<StaticNoiseTransition>(
            TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives)),
            rSettings)
        ;
}

}

std::shared_ptr<OGLTransitionImpl> makeStatic()
{
    Primitive Slide;

    Slide.pushTriangle (glm::vec2 (0,0), glm::vec2 (1,0), glm::vec2 (0,1));
    Slide.pushTriangle (glm::vec2 (1,0), glm::vec2 (0,1), glm::vec2 (1,1));
    Primitives_t aLeavingSlide;
    aLeavingSlide.push_back (Slide);
    Primitives_t aEnteringSlide;
    aEnteringSlide.push_back (Slide);

    TransitionSettings aSettings;
    aSettings.mbUseMipMapLeaving = aSettings.mbUseMipMapEntering = false;

    return makeStaticNoiseTransition(std::move(aLeavingSlide), std::move(aEnteringSlide), aSettings);
}

namespace
{

class DissolveTransition : public PermTextureTransition
{
public:
    DissolveTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : PermTextureTransition(rScene, rSettings)
    {}

private:
    virtual GLuint makeShader() const override;
};

GLuint DissolveTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"basicVertexShader"_ustr, u"dissolveFragmentShader"_ustr );
}

std::shared_ptr<OGLTransitionImpl>
makeDissolveTransition(
        Primitives_t&& rLeavingSlidePrimitives,
        Primitives_t&& rEnteringSlidePrimitives,
        const TransitionSettings& rSettings)
{
    return std::make_shared<DissolveTransition>(
            TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives)),
            rSettings)
        ;
}

}

std::shared_ptr<OGLTransitionImpl> makeDissolve()
{
    Primitive Slide;

    Slide.pushTriangle (glm::vec2 (0,0), glm::vec2 (1,0), glm::vec2 (0,1));
    Slide.pushTriangle (glm::vec2 (1,0), glm::vec2 (0,1), glm::vec2 (1,1));
    Primitives_t aLeavingSlide;
    aLeavingSlide.push_back (Slide);
    Primitives_t aEnteringSlide;
    aEnteringSlide.push_back (Slide);

    TransitionSettings aSettings;
    aSettings.mbUseMipMapLeaving = aSettings.mbUseMipMapEntering = false;

    return makeDissolveTransition(std::move(aLeavingSlide), std::move(aEnteringSlide), aSettings);
}

namespace
{

class VortexTransition : public PermTextureTransition
{
public:
    VortexTransition(const TransitionScene& rScene, const TransitionSettings& rSettings, int nNX, int nNY)
        : PermTextureTransition(rScene, rSettings)
        , maNumTiles(nNX,nNY)
    {
        mvTileInfo.resize(6*maNumTiles.x*maNumTiles.y);
        mnFramebuffers[0] = 0;
        mnFramebuffers[1] = 0;
        mnDepthTextures[0] = 0;
        mnDepthTextures[1] = 0;
    }

private:
    virtual void finishTransition() override;
    virtual GLuint makeShader() const override;
    virtual void prepareTransition( sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, OpenGLContext *pContext ) override;
    virtual void displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, double SlideWidthScale, double SlideHeightScale, OpenGLContext *pContext ) override;

    GLint mnSlideLocation = -1;
    GLint mnTileInfoLocation = -1;
    GLuint mnTileInfoBuffer = 0u;
    GLint mnShadowLocation = -1;
    std::array<GLuint, 2> mnFramebuffers;
    std::array<GLuint, 2> mnDepthTextures;

    glm::ivec2 maNumTiles;

    std::vector<GLfloat> mvTileInfo;
};

void VortexTransition::finishTransition()
{
    PermTextureTransition::finishTransition();

    CHECK_GL_ERROR();
    glDeleteTextures(2, mnDepthTextures.data());
    mnDepthTextures = {0u, 0u};
    CHECK_GL_ERROR();
    glDeleteFramebuffers(2, mnFramebuffers.data());
    mnFramebuffers = {0u, 0u};
    glDeleteBuffers(1, &mnTileInfoBuffer);
    mnTileInfoBuffer = 0u;
    mnSlideLocation = -1;
    mnTileInfoLocation = -1;
    mnShadowLocation = -1;
    CHECK_GL_ERROR();
}

GLuint VortexTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"vortexVertexShader"_ustr, u"vortexFragmentShader"_ustr, u"vortexGeometryShader"_ustr );
}

glm::mat4 lookAt(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up) {
    glm::vec3 f = glm::normalize(center - eye);
    glm::vec3 u = glm::normalize(up);
    glm::vec3 s = glm::normalize(glm::cross(f, u));
    u = glm::cross(s, f);

    return glm::mat4(s.x, u.x, -f.x, 0,
                     s.y, u.y, -f.y, 0,
                     s.z, u.z, -f.z, 0,
                     -glm::dot(s, eye), -glm::dot(u, eye), glm::dot(f, eye), 1);
}

void VortexTransition::prepareTransition( sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, OpenGLContext *pContext )
{
    CHECK_GL_ERROR();
    PermTextureTransition::prepareTransition( glLeavingSlideTex, glEnteringSlideTex, pContext );
    CHECK_GL_ERROR();

    mnSlideLocation = glGetUniformLocation(m_nProgramObject, "slide");
    mnTileInfoLocation = glGetAttribLocation(m_nProgramObject, "tileInfo");
    GLint nNumTilesLocation = glGetUniformLocation(m_nProgramObject, "numTiles");
    mnShadowLocation = glGetUniformLocation(m_nProgramObject, "shadow");
    GLint nOrthoProjectionMatrix = glGetUniformLocation(m_nProgramObject, "orthoProjectionMatrix");
    GLint nOrthoViewMatrix = glGetUniformLocation(m_nProgramObject, "orthoViewMatrix");
    GLint location = glGetUniformLocation(m_nProgramObject, "leavingShadowTexture");
    glUniform1i(location, 2);
    location = glGetUniformLocation(m_nProgramObject, "enteringShadowTexture");
    glUniform1i(location, 3);
    CHECK_GL_ERROR();

    glUniform2iv(nNumTilesLocation, 1, glm::value_ptr(maNumTiles));
    CHECK_GL_ERROR();

    glGenBuffers(1, &mnTileInfoBuffer);
    CHECK_GL_ERROR();

    // We store the (x,y) indexes of the tile each vertex belongs to in a float, so they must fit.
    assert(maNumTiles.x < 256);
    assert(maNumTiles.y < 256);

    // Two triangles, i.e. six vertices, per tile
    {
        int n = 0;
        for (int x = 0; x < maNumTiles.x; x++)
        {
            for (int y = 0; y < maNumTiles.y; y++)
            {
                for (int v = 0; v < 6; v++)
                {
                    mvTileInfo[n] = x + (y << 8) + (v << 16);
                    n++;
                }
            }
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, mnTileInfoBuffer);
    CHECK_GL_ERROR();
    glEnableVertexAttribArray(mnTileInfoLocation);
    CHECK_GL_ERROR();
    glVertexAttribPointer(mnTileInfoLocation, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    CHECK_GL_ERROR();
    glBufferData(GL_ARRAY_BUFFER, mvTileInfo.size()*sizeof(GLfloat), mvTileInfo.data(), GL_STATIC_DRAW);
    CHECK_GL_ERROR();

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_GL_ERROR();

    double EyePos(10.0);
    double const RealF(1.0);
    double const RealN(-1.0);
    double const RealL(-2.0);
    double RealR(2.0);
    double const RealB(-2.0);
    double RealT(2.0);
    double ClipN(EyePos+5.0*RealN);
    double ClipF(EyePos+15.0*RealF);
    double ClipL(RealL*8.0);
    double ClipR(RealR*8.0);
    double ClipB(RealB*8.0);
    double ClipT(RealT*8.0);

    glm::mat4 projection = glm::ortho<float>(ClipL, ClipR, ClipB, ClipT, ClipN, ClipF);
    //This scaling is to take the plane with BottomLeftCorner(-1,-1,0) and TopRightCorner(1,1,0) and map it to the screen after the perspective division.
    glm::vec3 scale(1.0 / (((RealR * 2.0 * ClipN) / (EyePos * (ClipR - ClipL))) - ((ClipR + ClipL) / (ClipR - ClipL))),
                    1.0 / (((RealT * 2.0 * ClipN) / (EyePos * (ClipT - ClipB))) - ((ClipT + ClipB) / (ClipT - ClipB))),
                    1.0);
    projection = glm::scale(projection, scale);
    glUniformMatrix4fv(nOrthoProjectionMatrix, 1, false, glm::value_ptr(projection));

    glm::mat4 view = lookAt(glm::vec3(-1, 1, EyePos), glm::vec3(-0.5, 0.5, 0), glm::vec3(0, 1, 0));
    glUniformMatrix4fv(nOrthoViewMatrix, 1, false, glm::value_ptr(view));

    // Generate the framebuffers and textures for the shadows.
    glGenTextures(2, mnDepthTextures.data());
    glGenFramebuffers(2, mnFramebuffers.data());

    for (int i : {0, 1}) {
        glBindTexture(GL_TEXTURE_2D, mnDepthTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, 2048, 2048, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, mnFramebuffers[i]);
        glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, mnDepthTextures[i], 0);
        glDrawBuffer(GL_NONE); // No color buffer is drawn to.

        // Always check that our framebuffer is ok
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SAL_WARN("slideshow.opengl", "Wrong framebuffer!");
            return;
        }
    }

    pContext->restoreDefaultFramebuffer();
    glBindTexture(GL_TEXTURE_2D, 0);

    glActiveTexture( GL_TEXTURE2 );
    glBindTexture( GL_TEXTURE_2D, mnDepthTextures[0] );
    glActiveTexture( GL_TEXTURE3 );
    glBindTexture( GL_TEXTURE_2D, mnDepthTextures[1] );
    glActiveTexture( GL_TEXTURE0 );
}

void VortexTransition::displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, double SlideWidthScale, double SlideHeightScale, OpenGLContext * pContext )
{
    CHECK_GL_ERROR();
    applyOverallOperations( nTime, SlideWidthScale, SlideHeightScale );
    glUniform1f( m_nTimeLocation, nTime );
    glUniform1f( mnShadowLocation, 1.0 );

    std::array<GLint, 4> viewport;
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    glViewport(0, 0, 2048, 2048);

    glBindFramebuffer(GL_FRAMEBUFFER, mnFramebuffers[0]);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUniform1f( mnSlideLocation, 0.0 );
    displaySlide( nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale );

    glBindFramebuffer(GL_FRAMEBUFFER, mnFramebuffers[1]);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUniform1f( mnSlideLocation, 1.0 );
    displaySlide( nTime, glEnteringSlideTex, getScene().getEnteringSlide(), SlideWidthScale, SlideHeightScale );

    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    pContext->restoreDefaultFramebuffer();
    glUniform1f( mnShadowLocation, 0.0 );
    glUniform1f( mnSlideLocation, 0.0 );
    displaySlide( nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale );
    glUniform1f( mnSlideLocation, 1.0 );
    displaySlide( nTime, glEnteringSlideTex, getScene().getEnteringSlide(), SlideWidthScale, SlideHeightScale );
    CHECK_GL_ERROR();
}

std::shared_ptr<OGLTransitionImpl>
makeVortexTransition(Primitives_t&& rLeavingSlidePrimitives,
                     Primitives_t&& rEnteringSlidePrimitives,
                     const TransitionSettings& rSettings,
                     int NX,
                     int NY)
{
    return std::make_shared<VortexTransition>(TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives)),
                                              rSettings,
                                              NX, NY);
}

}

std::shared_ptr<OGLTransitionImpl> makeVortex()
{
    const int NX = 96, NY = 96;
    Primitive Slide;

    for (int x = 0; x < NX; x++)
    {
        for (int y = 0; y < NY; y++)
        {
            Slide.pushTriangle (glm::vec2 (fdiv(x,NX),fdiv(y,NY)), glm::vec2 (fdiv(x+1,NX),fdiv(y,NY)), glm::vec2 (fdiv(x,NX),fdiv(y+1,NY)));
            Slide.pushTriangle (glm::vec2 (fdiv(x+1,NX),fdiv(y,NY)), glm::vec2 (fdiv(x,NX),fdiv(y+1,NY)), glm::vec2 (fdiv(x+1,NX),fdiv(y+1,NY)));
        }
    }
    Primitives_t aLeavingSlide;
    aLeavingSlide.push_back (Slide);
    Primitives_t aEnteringSlide;
    aEnteringSlide.push_back (Slide);

    TransitionSettings aSettings;
    aSettings.mbUseMipMapLeaving = aSettings.mbUseMipMapEntering = false;
    aSettings.mnRequiredGLVersion = 3.2f;

    return makeVortexTransition(std::move(aLeavingSlide), std::move(aEnteringSlide), aSettings, NX, NY);
}

namespace
{

class RippleTransition : public OGLTransitionImpl
{
public:
    RippleTransition(const TransitionScene& rScene, const TransitionSettings& rSettings, const glm::vec2& rCenter)
        : OGLTransitionImpl(rScene, rSettings),
          maCenter(rCenter)
    {
    }

private:
    virtual GLuint makeShader() const override;
    virtual void prepareTransition( sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, OpenGLContext *pContext ) override;
    virtual void prepare( double SlideWidth, double SlideHeight ) override;

    glm::vec2 maCenter;
    GLint maSlideRatioLocation = -1;
};

GLuint RippleTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"basicVertexShader"_ustr, u"rippleFragmentShader"_ustr );
}

void RippleTransition::prepareTransition( sal_Int32, sal_Int32, OpenGLContext* )
{
    GLint nCenterLocation = glGetUniformLocation(m_nProgramObject, "center");
    CHECK_GL_ERROR();

    glUniform2fv(nCenterLocation, 1, glm::value_ptr(maCenter));
    CHECK_GL_ERROR();

    maSlideRatioLocation = glGetUniformLocation(m_nProgramObject, "slideRatio");
    CHECK_GL_ERROR();
}

void RippleTransition::prepare( double SlideWidth, double SlideHeight )
{
    if( maSlideRatioLocation != -1 )
        glUniform1f( maSlideRatioLocation, SlideWidth / SlideHeight );
}

std::shared_ptr<OGLTransitionImpl>
makeRippleTransition(Primitives_t&& rLeavingSlidePrimitives,
                     Primitives_t&& rEnteringSlidePrimitives,
                     const TransitionSettings& rSettings)
{
    // The center point should be adjustable by the user, but we have no way to do that in the UI
    return std::make_shared<RippleTransition>(TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives)),
                                              rSettings,
                                              glm::vec2(0.5, 0.5));
}

}

std::shared_ptr<OGLTransitionImpl> makeRipple()
{
    Primitive Slide;

    Slide.pushTriangle (glm::vec2 (0,0), glm::vec2 (1,0), glm::vec2 (0,1));
    Slide.pushTriangle (glm::vec2 (1,0), glm::vec2 (0,1), glm::vec2 (1,1));

    Primitives_t aLeavingSlide;
    aLeavingSlide.push_back (Slide);

    Primitives_t aEnteringSlide;
    aEnteringSlide.push_back (Slide);

    TransitionSettings aSettings;
    aSettings.mbUseMipMapLeaving = aSettings.mbUseMipMapEntering = false;

    return makeRippleTransition(std::move(aLeavingSlide), std::move(aEnteringSlide), aSettings);
}

static void createHexagon(Primitive& aHexagon, const int x, const int y, const int NX, const int NY)
{
    if (y % 4 == 0)
    {
        aHexagon.pushTriangle(vec(x-1, y-1, NX, NY), vec(x,   y-2, NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x,   y-2, NX, NY), vec(x+1, y-1, NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x+1, y-1, NX, NY), vec(x+1, y,   NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x+1, y,   NX, NY), vec(x,   y+1, NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x,   y+1, NX, NY), vec(x-1, y,   NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x-1, y,   NX, NY), vec(x-1, y-1, NX, NY), vec(x, y+0.5, NX, NY));
    }
    else
    {
        aHexagon.pushTriangle(vec(x-2, y-1, NX, NY), vec(x-1, y-2, NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x-1, y-2, NX, NY), vec(x,   y-1, NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x,   y-1, NX, NY), vec(x,   y,   NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x,   y,   NX, NY), vec(x-1, y+1, NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x-1, y+1, NX, NY), vec(x-2, y,   NX, NY), vec(x, y+0.5, NX, NY));
        aHexagon.pushTriangle(vec(x-2, y,   NX, NY), vec(x-2, y-1, NX, NY), vec(x, y+0.5, NX, NY));
    }
}

namespace
{

class GlitterTransition : public PermTextureTransition
{
public:
    GlitterTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : PermTextureTransition(rScene, rSettings)
    {
    }

private:
    virtual GLuint makeShader() const override;
    virtual void prepareTransition( sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, OpenGLContext *pContext ) override;
    virtual void cleanup() override;

    GLuint maBuffer = 0;
};

GLuint GlitterTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"glitterVertexShader"_ustr, u"glitterFragmentShader"_ustr );
}

struct ThreeFloats
{
    GLfloat x, y, z;
};

void GlitterTransition::prepareTransition( sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, OpenGLContext *pContext )
{
    CHECK_GL_ERROR();
    PermTextureTransition::prepareTransition( glLeavingSlideTex, glEnteringSlideTex, pContext );
    CHECK_GL_ERROR();

    GLint nNumTilesLocation = glGetUniformLocation(m_nProgramObject, "numTiles");
    if (nNumTilesLocation != -1) {
        glUniform2iv(nNumTilesLocation, 1, glm::value_ptr(glm::ivec2(41, 41 * 4 / 3)));
        CHECK_GL_ERROR();
    }

    glGenBuffers(1, &maBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, maBuffer);

    // Upload the center of each hexagon.
    const Primitive& primitive = getScene().getLeavingSlide()[0];
    std::vector<ThreeFloats> vertices;
    for (int i = 2; i < primitive.getVerticesCount(); i += 18) {
        const glm::vec3& center = primitive.getVertex(i);
        for (int j = 0; j < 18; ++j)
            vertices.push_back({center.x, center.y, center.z});
    }
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * 3 * sizeof(GLfloat), vertices.data(), GL_STATIC_DRAW);

    GLint location = glGetAttribLocation(m_nProgramObject, "center");
    if (location != -1) {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer( location, 3, GL_FLOAT, false, 0, nullptr );
        CHECK_GL_ERROR();
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GlitterTransition::cleanup()
{
    CHECK_GL_ERROR();
    glDeleteBuffers(1, &maBuffer);
    CHECK_GL_ERROR();
}

std::shared_ptr<OGLTransitionImpl>
makeGlitterTransition(Primitives_t&& rLeavingSlidePrimitives,
                      Primitives_t&& rEnteringSlidePrimitives,
                      const TransitionSettings& rSettings)
{
    return std::make_shared<GlitterTransition>(TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives)),
                                               rSettings);
}

}

std::shared_ptr<OGLTransitionImpl> makeGlitter()
{
    const int NX = 80;
    const int NY = NX * 4 / 3;

    Primitives_t aSlide;
    Primitives_t aEmptySlide;
    Primitive aHexagon;

    for (int y = 0; y < NY+2; y+=2)
        for (int x = 0; x < NX+2; x+=2)
            createHexagon(aHexagon, x, y, NX, NY);

    aSlide.push_back(aHexagon);

    return makeGlitterTransition(std::move(aSlide), std::move(aEmptySlide), TransitionSettings());
}

namespace
{

class HoneycombTransition : public PermTextureTransition
{
public:
    HoneycombTransition(const TransitionScene& rScene, const TransitionSettings& rSettings)
        : PermTextureTransition(rScene, rSettings)
    {
        mnDepthTextures[0] = 0;
        mnDepthTextures[1] = 0;
    }

private:
    virtual void finishTransition() override;
    virtual GLuint makeShader() const override;
    virtual void prepareTransition( sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, OpenGLContext *pContext ) override;
    virtual void displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, double SlideWidthScale, double SlideHeightScale, OpenGLContext *pContext ) override;

    GLint maHexagonSizeLocation = -1;
    GLint maSelectedTextureLocation = -1;
    GLint mnShadowLocation = -1;
    GLuint mnFramebuffer = 0u;
    std::array<GLuint, 2> mnDepthTextures;
};

void HoneycombTransition::finishTransition()
{
    PermTextureTransition::finishTransition();

    CHECK_GL_ERROR();
    glActiveTexture( GL_TEXTURE2 );
    glBindTexture( GL_TEXTURE_2D, 0 );
    glActiveTexture( GL_TEXTURE3 );
    glBindTexture( GL_TEXTURE_2D, 0 );
    glActiveTexture( GL_TEXTURE0 );
    CHECK_GL_ERROR();
    glDeleteTextures(2, mnDepthTextures.data());
    mnDepthTextures = {0u, 0u};
    CHECK_GL_ERROR();
    glDeleteFramebuffers(1, &mnFramebuffer);
    mnFramebuffer = 0u;
    CHECK_GL_ERROR();
}

GLuint HoneycombTransition::makeShader() const
{
    return OpenGLHelper::LoadShaders( u"honeycombVertexShader"_ustr, u"honeycombFragmentShader"_ustr, u"honeycombGeometryShader"_ustr );
}

void HoneycombTransition::prepareTransition( sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex, OpenGLContext *pContext )
{
    CHECK_GL_ERROR();
    PermTextureTransition::prepareTransition( glLeavingSlideTex, glEnteringSlideTex, pContext );

    CHECK_GL_ERROR();
    maHexagonSizeLocation = glGetUniformLocation(m_nProgramObject, "hexagonSize");
    maSelectedTextureLocation = glGetUniformLocation( m_nProgramObject, "selectedTexture" );
    mnShadowLocation = glGetUniformLocation(m_nProgramObject, "shadow");
    GLint nOrthoProjectionMatrix = glGetUniformLocation(m_nProgramObject, "orthoProjectionMatrix");
    GLint nOrthoViewMatrix = glGetUniformLocation(m_nProgramObject, "orthoViewMatrix");
    GLint location = glGetUniformLocation(m_nProgramObject, "colorShadowTexture");
    glUniform1i(location, 2);
    location = glGetUniformLocation(m_nProgramObject, "depthShadowTexture");
    glUniform1i(location, 3);
    CHECK_GL_ERROR();

    // We want to see the entering slide behind the leaving one.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    CHECK_GL_ERROR();

    double EyePos(10.0);
    double const RealF(1.0);
    double const RealN(-1.0);
    double const RealL(-4.0);
    double RealR(4.0);
    double const RealB(-4.0);
    double RealT(4.0);
    double ClipN(EyePos+5.0*RealN);
    double ClipF(EyePos+15.0*RealF);
    double ClipL(RealL*8.0);
    double ClipR(RealR*8.0);
    double ClipB(RealB*8.0);
    double ClipT(RealT*8.0);

    glm::mat4 projection = glm::ortho<float>(ClipL, ClipR, ClipB, ClipT, ClipN, ClipF);
    //This scaling is to take the plane with BottomLeftCorner(-1,-1,0) and TopRightCorner(1,1,0) and map it to the screen after the perspective division.
    glm::vec3 scale(1.0 / (((RealR * 2.0 * ClipN) / (EyePos * (ClipR - ClipL))) - ((ClipR + ClipL) / (ClipR - ClipL))),
                    1.0 / (((RealT * 2.0 * ClipN) / (EyePos * (ClipT - ClipB))) - ((ClipT + ClipB) / (ClipT - ClipB))),
                    1.0);
    projection = glm::scale(projection, scale);
    glUniformMatrix4fv(nOrthoProjectionMatrix, 1, false, glm::value_ptr(projection));

    glm::mat4 view = lookAt(glm::vec3(0, 0, EyePos), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    glUniformMatrix4fv(nOrthoViewMatrix, 1, false, glm::value_ptr(view));

    // Generate the framebuffer and textures for the shadows.
    glGenTextures(2, mnDepthTextures.data());
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mnDepthTextures[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2048, 2048, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, mnDepthTextures[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, 2048, 2048, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE0);
    glGenFramebuffers(1, &mnFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, mnFramebuffer);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, mnDepthTextures[0], 0);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, mnDepthTextures[1], 0);

    // Always check that our framebuffer is ok
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        SAL_WARN("slideshow.opengl", "Wrong framebuffer!");
        return;
    }

    pContext->restoreDefaultFramebuffer();
}

void HoneycombTransition::displaySlides_( double nTime, sal_Int32 glLeavingSlideTex, sal_Int32 glEnteringSlideTex,
                                          double SlideWidthScale, double SlideHeightScale, OpenGLContext *pContext )
{
    CHECK_GL_ERROR();
    applyOverallOperations(nTime, SlideWidthScale, SlideHeightScale);
    glUniform1f(m_nTimeLocation, nTime);
    glUniform1f(mnShadowLocation, 1.0);
    CHECK_GL_ERROR();

    const float borderSize = 0.15f;

    std::array<GLint, 4> viewport;
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    glViewport(0, 0, 2048, 2048);
    glBindFramebuffer(GL_FRAMEBUFFER, mnFramebuffer);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUniform1f(mnShadowLocation, 1.0);
    glUniform1f(maSelectedTextureLocation, 1.0);
    glUniform1f(maHexagonSizeLocation, 1.0f - borderSize);
    displaySlide(nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale);
    glUniform1f(maHexagonSizeLocation, 1.0f + borderSize);
    displaySlide(nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale);

    // The back (entering) slide needs to be drawn before the front (leaving) one in order for blending to work.
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    pContext->restoreDefaultFramebuffer();
    glUniform1f(mnShadowLocation, 0.0);
    glUniform1f(maSelectedTextureLocation, 0.0);
    glUniform1f(maHexagonSizeLocation, 1.0f - borderSize);
    displaySlide(nTime, glEnteringSlideTex, getScene().getEnteringSlide(), SlideWidthScale, SlideHeightScale);
    glUniform1f(maHexagonSizeLocation, 1.0f + borderSize);
    displaySlide(nTime, glEnteringSlideTex, getScene().getEnteringSlide(), SlideWidthScale, SlideHeightScale);
    glUniform1f(maSelectedTextureLocation, 1.0);
    glUniform1f(maHexagonSizeLocation, 1.0f - borderSize);
    displaySlide(nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale);
    glUniform1f(maHexagonSizeLocation, 1.0f + borderSize);
    displaySlide(nTime, glLeavingSlideTex, getScene().getLeavingSlide(), SlideWidthScale, SlideHeightScale);
    CHECK_GL_ERROR();
}

std::shared_ptr<OGLTransitionImpl>
makeHoneycombTransition(Primitives_t&& rLeavingSlidePrimitives,
                        Primitives_t&& rEnteringSlidePrimitives,
                        const TransitionSettings& rSettings)
{
    // The center point should be adjustable by the user, but we have no way to do that in the UI
    return std::make_shared<HoneycombTransition>(TransitionScene(std::move(rLeavingSlidePrimitives), std::move(rEnteringSlidePrimitives)),
                                                 rSettings);
}

}

std::shared_ptr<OGLTransitionImpl> makeHoneycomb()
{
    const int NX = 21;
    const int NY = 21;

    TransitionSettings aSettings;
    aSettings.mnRequiredGLVersion = 3.2f;

    Primitives_t aSlide;
    Primitive aHexagon;
    for (int y = 0; y < NY+2; y+=2)
        for (int x = 0; x < NX+2; x+=2)
            aHexagon.pushTriangle(glm::vec2((y % 4) ? fdiv(x, NX) : fdiv(x + 1, NX), fdiv(y, NY)), glm::vec2(1, 0), glm::vec2(0, 0));
    aSlide.push_back(aHexagon);

    return makeHoneycombTransition(std::vector(aSlide), std::vector(aSlide), aSettings);
}

std::shared_ptr<OGLTransitionImpl> makeNewsflash()
{
    Primitive Slide;

    Slide.pushTriangle(glm::vec2(0,0),glm::vec2(1,0),glm::vec2(0,1));
    Slide.pushTriangle(glm::vec2(1,0),glm::vec2(0,1),glm::vec2(1,1));
    Slide.Operations.push_back(makeSRotate(glm::vec3(0,0,1),glm::vec3(0,0,0),3000,true,0,0.5));
    Slide.Operations.push_back(makeSScale(glm::vec3(0.01,0.01,0.01),glm::vec3(0,0,0),true,0,0.5));
    Slide.Operations.push_back(makeSTranslate(glm::vec3(-10000, 0, 0),false, 0.5, 2));
    Primitives_t aLeavingSlide;
    aLeavingSlide.push_back(Slide);

    Slide.Operations.clear();
    Slide.Operations.push_back(makeSRotate(glm::vec3(0,0,1),glm::vec3(0,0,0),-3000,true,0.5,1));
    Slide.Operations.push_back(makeSTranslate(glm::vec3(-100, 0, 0),false, -1, 1));
    Slide.Operations.push_back(makeSTranslate(glm::vec3(100, 0, 0),false, 0.5, 1));
    Slide.Operations.push_back(makeSScale(glm::vec3(0.01,0.01,0.01),glm::vec3(0,0,0),false,-1,1));
    Slide.Operations.push_back(makeSScale(glm::vec3(100,100,100),glm::vec3(0,0,0),true,0.5,1));
    Primitives_t aEnteringSlide;
    aEnteringSlide.push_back(Slide);

    Operations_t aOverallOperations;
    aOverallOperations.push_back(makeSRotate(glm::vec3(0,0,1),glm::vec3(0.2,0.2,0),1080,true,0,1));

    return makeSimpleTransition(std::move(aLeavingSlide), std::move(aEnteringSlide), std::move(aOverallOperations));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
