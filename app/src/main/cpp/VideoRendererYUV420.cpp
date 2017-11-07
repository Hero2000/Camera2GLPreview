#include "VideoRendererYUV420.h"

#include <cmath>

// Vertices for a full screen quad.
static const float kVertices[8] = {
  -1.f, 1.f,
  -1.f, -1.f,
  1.f, 1.f,
  1.f, -1.f
};

// Texture coordinates for mapping entire texture.
static const float kTextureCoords[8] = {
  0, 0,
  0, 1,
  1, 0,
  1, 1
};

// Vertex shader.
static const char kVertexShader[] =
    "#version 100\n"
    "varying vec2 v_texcoord;\n"
    "attribute vec4 position;\n"
    "attribute vec2 texcoord;\n"
	"uniform mat4 projection;\n"
    "uniform mat4 model_view;\n"
    "void main() {\n"
    "   v_texcoord = texcoord;\n"
    "   gl_Position = projection * model_view * position;\n"
    "}\n";

// YUV420 to RGB conversion, pixel shader.
static const char kFragmentShader[] =
    "#version 100\n"
    "precision highp float;"
    "varying vec2 v_texcoord;\n"
    "uniform lowp sampler2D s_textureY;\n"
    "uniform lowp sampler2D s_textureU;\n"
    "uniform lowp sampler2D s_textureV;\n"
    "void main() {\n"
    "   float y, u, v, r, g, b;\n"
    "   y = texture2D(s_textureY, v_texcoord).r;\n"
    "   u = texture2D(s_textureU, v_texcoord).r;\n"
    "   v = texture2D(s_textureV, v_texcoord).r;\n"
    "   u = u - 0.5;\n"
    "   v = v - 0.5;\n"
    "   r = y + 1.403 * v;\n"
    "   g = y - 0.344 * u - 0.714 * v;\n"
    "   b = y + 1.770 * u;\n"
    "   gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

VideoRendererYUV420::VideoRendererYUV420()
    : m_length(0)
	, m_pDataY(nullptr)
	, m_pDataU(nullptr)
	, m_pDataV(nullptr)
	, m_sizeY(0)
	, m_sizeU(0)
	, m_sizeV(0)
	, m_texIdY(0)
	, m_texIdU(0)
	, m_texIdV(0)
	, m_vertexPos(0)
    , m_texLoc(0)
	, m_texYLoc(0)
	, m_texULoc(0)
	, m_texVLoc(0)
	, m_uniformMatrix(0)
{
	isOrientationChanged = true;
}

VideoRendererYUV420::~VideoRendererYUV420()
{
	deleteTextures();
}

void VideoRendererYUV420::render()
{
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	if (!updateTextures() || !useProgram()) return;

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    check_gl_error("glDrawArrays");
}

void VideoRendererYUV420::updateFrame(const video_frame& frame)
{
	m_sizeY = frame.width * frame.height;
	m_sizeU = frame.width * frame.height / 4;
	m_sizeV = frame.width * frame.height / 4;

	if (m_pDataY == nullptr || m_width != frame.width || m_height != frame.height)
    {
        m_pDataY = std::make_unique<uint8_t[]>(m_sizeY + m_sizeU + m_sizeV);
		m_pDataU = std::unique_ptr<uint8_t[]>(m_pDataY.get() + m_sizeY);
		m_pDataV = std::unique_ptr<uint8_t[]>(m_pDataU.get() + m_sizeU);
	}

	m_width = frame.width;
	m_height = frame.height;

	if (m_width == frame.stride_y)
    {
		memcpy(m_pDataY.get(), frame.y, m_sizeY);
	}
    else
    {
		uint8_t* pSrcY = frame.y;
		uint8_t* pDstY = m_pDataY.get();

		for (int h = 0; h < m_height; h++)
        {
			memcpy(pDstY, pSrcY, m_width);

			pSrcY += frame.stride_y;
			pDstY += m_width;
		}
	}

	if (m_width / 2 == frame.stride_uv)
    {
		memcpy(m_pDataU.get(), frame.u, m_sizeU);
		memcpy(m_pDataV.get(), frame.v, m_sizeV);
	}
    else
    {
		uint8_t* pSrcU = frame.u;
		uint8_t* pSrcV = frame.v;
		uint8_t* pDstU = m_pDataU.get();
		uint8_t* pDstV = m_pDataV.get();

		for (int h = 0; h < m_height / 2; h++)
        {
			memcpy(pDstU, pSrcU, m_width/2);
			memcpy(pDstV, pSrcV, m_width/2);

			pDstU += m_width/2;
			pDstV += m_width/2;

			pSrcU += frame.stride_uv;
			pSrcV += frame.stride_uv;
		}
	}

	isDirty = true;
}

void VideoRendererYUV420::draw(uint8_t *buffer, size_t length, size_t width, size_t height)
{
    m_length = length;

	video_frame frame;
	frame.width = width;
	frame.height = height;
	frame.stride_y = width;
	frame.stride_uv = width / 2;
	frame.y = buffer;
	frame.u = buffer + width * height;
	frame.v = buffer + width * height * 5 / 4;

	updateFrame(frame);
}

bool VideoRendererYUV420::createTextures()
{
    GLsizei widthY = (GLsizei)m_width;
    GLsizei heightY = (GLsizei)m_height;

	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &m_texIdY);
	glBindTexture(GL_TEXTURE_2D, m_texIdY);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, widthY, heightY, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

    check_gl_error("Create Y texture");

	GLsizei widthU = (GLsizei)m_width / 2;
    GLsizei heightU = (GLsizei)m_height / 2;

	glActiveTexture(GL_TEXTURE1);
	glGenTextures(1, &m_texIdU);
	glBindTexture(GL_TEXTURE_2D, m_texIdU);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, widthU, heightU, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

    check_gl_error("Create U texture");

    GLsizei widthV = (GLsizei)m_width / 2;
    GLsizei heightV = (GLsizei)m_height / 2;

	glActiveTexture(GL_TEXTURE2);
	glGenTextures(1, &m_texIdV);
	glBindTexture(GL_TEXTURE_2D, m_texIdV);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, widthV, heightV, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

    check_gl_error("Create V texture");

	return true;
}

bool VideoRendererYUV420::updateTextures()
{
	if (!m_texIdY && !m_texIdU && !m_texIdV && !createTextures()) return false;

	if (isDirty)
    {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_texIdY);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, (GLsizei)m_width, (GLsizei)m_height, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, m_pDataY.get());

        check_gl_error("Update Y texture");

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, m_texIdU);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, (GLsizei)m_width / 2, (GLsizei)m_height / 2, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, m_pDataU.get());

        check_gl_error("Update U texture");

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, m_texIdV);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, (GLsizei)m_width / 2, (GLsizei)m_height / 2, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, m_pDataV.get());

        check_gl_error("Update V texture");

        isDirty = false;

        return true;
	}

	return false;
}

void VideoRendererYUV420::deleteTextures()
{
	if (m_texIdY)
    {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteTextures(1, &m_texIdY);

        check_gl_error("Delete Y texture");

		m_texIdY = 0;
	}

	if (m_texIdU)
    {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteTextures(1, &m_texIdU);

        check_gl_error("Delete U texture");

		m_texIdU = 0;
	}

	if (m_texIdV)
    {
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteTextures(1, &m_texIdV);

        check_gl_error("Delete V texture");

		m_texIdV = 0;
	}
}

GLuint VideoRendererYUV420::createProgram()
{
	m_program = ::create_program(kVertexShader, kFragmentShader, m_vertexShader, m_pixelShader);
	if (!m_program)
    {
        check_gl_error("Create program");
		LOGE("Could not create program.");
	}

	m_vertexPos = (GLuint)glGetAttribLocation(m_program, "position");
    check_gl_error("glGetAttribLocation position");

	m_uniformMatrix = glGetUniformLocation(m_program, "projection");
    check_gl_error("glGetUniformLocation modelviewProj");

	m_texYLoc = glGetUniformLocation(m_program, "s_textureY");
    check_gl_error("glGetUniformLocation s_textureY");
	m_texULoc = glGetUniformLocation(m_program, "s_textureU");
    check_gl_error("glGetUniformLocation s_textureU");
	m_texVLoc = glGetUniformLocation(m_program, "s_textureV");
    check_gl_error("glGetUniformLocation s_textureV");

	m_texLoc = (GLuint)glGetAttribLocation(m_program, "texcoord");
    check_gl_error("glGetAttribLocation texcoord.");

	return m_program;
}

GLuint VideoRendererYUV420::useProgram()
{
	if (!m_program && !createProgram())
    {
		LOGE("Could not use program.");
		return 0;
	}

	if (isOrientationChanged)
    {
		glUseProgram(m_program);

        check_gl_error("Use program.");

		// Bind parameters.
		glVertexAttribPointer(m_vertexPos, 2, GL_FLOAT, GL_FALSE, 0, kVertices);
        check_gl_error("glVertexAttribPointer");
		glEnableVertexAttribArray(m_vertexPos);
        check_gl_error("glEnableVertexAttribArray");

		GLfloat projection[16];
        mat4f_load_ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, projection);
		glUniformMatrix4fv(m_uniformMatrix, 1, GL_FALSE, projection);

        applyRotationZ(90);

		glUniform1i(m_texYLoc, 0);
        check_gl_error("Set uniform s_textureY.");

		glUniform1i(m_texULoc, 1);
        check_gl_error("Set uniform s_textureU.");

		glUniform1i(m_texVLoc, 2);
        check_gl_error("Set uniform s_textureV.");

		glVertexAttribPointer(m_texLoc, 2, GL_FLOAT, GL_FALSE, 0, kTextureCoords);
        check_gl_error("glVertexAttribPointer");
		glEnableVertexAttribArray(m_texLoc);
        check_gl_error("glEnableVertexAttribArray");

		isOrientationChanged = false;
	}

	return m_program;
}

void VideoRendererYUV420::applyRotationZ(float degrees)
{
    float radians = degrees * 3.14159f / 180.0f;
    float s = std::sin(radians);
    float c = std::cos(radians);
    float zRotation[16] = {
            c, -s, 0, 0,
            s, c, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
    };

    GLint modelviewUniform = glGetUniformLocation(m_program, "model_view");
    glUniformMatrix4fv(modelviewUniform, 1, 0, &zRotation[0]);
}