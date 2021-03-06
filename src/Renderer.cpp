// Copyright © 2014 Mikko Ronkainen <firstname@mikkoronkainen.com>
// License: GPLv3, see the LICENSE file.

#include <QOpenGLPixelTransferOptions>

#include "Renderer.h"
#include "VideoDecoder.h"
#include "MapImageReader.h"
#include "VideoStabilizer.h"
#include "InputHandler.h"
#include "RouteManager.h"
#include "Settings.h"
#include "FrameData.h"

using namespace OrientView;

bool Renderer::initialize(VideoDecoder* videoDecoder, MapImageReader* mapImageReader, VideoStabilizer* videoStabilizer, InputHandler* inputHandler, RouteManager* routeManager, Settings* settings)
{
	qDebug("Initializing renderer");

	this->videoStabilizer = videoStabilizer;
	this->inputHandler = inputHandler;
	this->routeManager = routeManager;

	videoPanel.clearColor = settings->video.backgroundColor;
	videoPanel.clippingEnabled = settings->video.enableClipping;
	videoPanel.clearingEnabled = settings->video.enableClearing;
	videoPanel.userX = settings->video.x;
	videoPanel.userY = settings->video.y;
	videoPanel.userAngle = settings->video.angle;
	videoPanel.userScale = settings->video.scale;
	videoPanel.textureWidth = videoDecoder->getFrameWidth();
	videoPanel.textureHeight = videoDecoder->getFrameHeight();
	videoPanel.texelWidth = 1.0 / videoPanel.textureWidth;
	videoPanel.texelHeight = 1.0 / videoPanel.textureHeight;

	mapPanel.clearColor = settings->map.backgroundColor;
	mapPanel.userX = settings->map.x;
	mapPanel.userY = settings->map.y;
	mapPanel.userAngle = settings->map.angle;
	mapPanel.userScale = settings->map.scale;
	mapPanel.textureWidth = mapImageReader->getMapImage().width();
	mapPanel.textureHeight = mapImageReader->getMapImage().height();
	mapPanel.texelWidth = 1.0 / mapPanel.textureWidth;
	mapPanel.texelHeight = 1.0 / mapPanel.textureHeight;
	mapPanel.relativeWidth = settings->map.relativeWidth;

	multisamples = settings->window.multisamples;
	showInfoPanel = settings->window.showInfoPanel;

	const double movingAverageAlpha = 0.1;
	averageFps.setAlpha(movingAverageAlpha);
	averageFrameTime.setAlpha(movingAverageAlpha);
	averageDecodeTime.setAlpha(movingAverageAlpha);
	averageStabilizeTime.setAlpha(movingAverageAlpha);
	averageRenderTime.setAlpha(movingAverageAlpha);
	averageEncodeTime.setAlpha(movingAverageAlpha);
	averageSpareTime.setAlpha(movingAverageAlpha);

	initializeOpenGLFunctions();

	if (!windowResized(settings->window.width, settings->window.height))
		return false;

	if (!loadShaders(videoPanel, settings->video.rescaleShader))
		return false;

	if (!loadShaders(mapPanel, settings->map.rescaleShader))
		return false;

	// 1 2
	// 4 3
	GLfloat videoPanelBuffer[] =
	{
		-(float)videoPanel.textureWidth / 2, (float)videoPanel.textureHeight / 2, 0.0f, // 1
		(float)videoPanel.textureWidth / 2, (float)videoPanel.textureHeight / 2, 0.0f, // 2
		(float)videoPanel.textureWidth / 2, -(float)videoPanel.textureHeight / 2, 0.0f, // 3
		-(float)videoPanel.textureWidth / 2, -(float)videoPanel.textureHeight / 2, 0.0f, // 4

		0.0f, 0.0f, // 1
		1.0f, 0.0f, // 2
		1.0f, 1.0f, // 3
		0.0f, 1.0f  // 4
	};

	// 1 2
	// 4 3
	GLfloat mapPanelBuffer[] =
	{
		-(float)mapPanel.textureWidth / 2, (float)mapPanel.textureHeight / 2, 0.0f, // 1
		(float)mapPanel.textureWidth / 2, (float)mapPanel.textureHeight / 2, 0.0f, // 2
		(float)mapPanel.textureWidth / 2, -(float)mapPanel.textureHeight / 2, 0.0f, // 3
		-(float)mapPanel.textureWidth / 2, -(float)mapPanel.textureHeight / 2, 0.0f, // 4

		0.0f, 0.0f, // 1
		1.0f, 0.0f, // 2
		1.0f, 1.0f, // 3
		0.0f, 1.0f  // 4
	};

	loadBuffer(videoPanel, videoPanelBuffer, 20);
	loadBuffer(mapPanel, mapPanelBuffer, 20);

	videoPanel.texture = new QOpenGLTexture(QOpenGLTexture::Target2D);
	videoPanel.texture->create();
	videoPanel.texture->bind();
	videoPanel.texture->setSize(videoPanel.textureWidth, videoPanel.textureHeight);
	videoPanel.texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
	videoPanel.texture->setMinificationFilter(QOpenGLTexture::Linear);
	videoPanel.texture->setMagnificationFilter(QOpenGLTexture::Linear);
	videoPanel.texture->setWrapMode(QOpenGLTexture::ClampToEdge);
	videoPanel.texture->allocateStorage();
	videoPanel.texture->release();

	mapPanel.texture = new QOpenGLTexture(mapImageReader->getMapImage());
	mapPanel.texture->bind();
	mapPanel.texture->setMinificationFilter(QOpenGLTexture::Linear);
	mapPanel.texture->setMagnificationFilter(QOpenGLTexture::Linear);
	mapPanel.texture->setWrapMode(QOpenGLTexture::ClampToEdge);
	mapPanel.texture->release();

	paintDevice = new QOpenGLPaintDevice();
	painter = new QPainter();

	return true;
}

bool Renderer::windowResized(int newWidth, int newHeight)
{
	windowWidth = newWidth;
	windowHeight = newHeight;

	fullClearRequested = true;

	QOpenGLFramebufferObjectFormat format;
	format.setSamples(multisamples);
	format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);

	if (outputFramebuffer != nullptr)
	{
		delete outputFramebuffer;
		outputFramebuffer = nullptr;
	}

	outputFramebuffer = new QOpenGLFramebufferObject(windowWidth, windowHeight, format);

	if (!outputFramebuffer->isValid())
	{
		qWarning("Could not create main frame buffer");
		return false;
	}

	format.setSamples(0);

	if (outputFramebufferNonMultisample != nullptr)
	{
		delete outputFramebufferNonMultisample;
		outputFramebufferNonMultisample = nullptr;
	}

	outputFramebufferNonMultisample = new QOpenGLFramebufferObject(windowWidth, windowHeight, format);

	if (!outputFramebufferNonMultisample->isValid())
	{
		qWarning("Could not create non multisampled main frame buffer");
		return false;
	}

	if (renderedFrameData.data != nullptr)
	{
		delete renderedFrameData.data;
		renderedFrameData.data = nullptr;
	}

	renderedFrameData = FrameData();
	renderedFrameData.dataLength = (size_t)(windowWidth * windowHeight * 4);
	renderedFrameData.rowLength = (size_t)(windowWidth * 4);
	renderedFrameData.data = new uint8_t[renderedFrameData.dataLength];
	renderedFrameData.width = windowWidth;
	renderedFrameData.height = windowHeight;

	return true;
}

Renderer::~Renderer()
{
	if (renderedFrameData.data != nullptr)
	{
		delete renderedFrameData.data;
		renderedFrameData.data = nullptr;
	}

	if (outputFramebufferNonMultisample != nullptr)
	{
		delete outputFramebufferNonMultisample;
		outputFramebufferNonMultisample = nullptr;
	}

	if (outputFramebuffer != nullptr)
	{
		delete outputFramebuffer;
		outputFramebuffer = nullptr;
	}

	if (painter != nullptr)
	{
		delete painter;
		painter = nullptr;
	}

	if (paintDevice != nullptr)
	{
		delete paintDevice;
		paintDevice = nullptr;
	}

	if (mapPanel.texture != nullptr)
	{
		delete mapPanel.texture;
		mapPanel.texture = nullptr;
	}

	if (videoPanel.texture != nullptr)
	{
		delete videoPanel.texture;
		videoPanel.texture = nullptr;
	}

	if (mapPanel.buffer != nullptr)
	{
		delete mapPanel.buffer;
		mapPanel.buffer = nullptr;
	}

	if (videoPanel.buffer != nullptr)
	{
		delete videoPanel.buffer;
		videoPanel.buffer = nullptr;
	}

	if (mapPanel.program != nullptr)
	{
		delete mapPanel.program;
		mapPanel.program = nullptr;
	}

	if (videoPanel.program != nullptr)
	{
		delete videoPanel.program;
		videoPanel.program = nullptr;
	}
}

bool Renderer::loadShaders(Panel& panel, const QString& shaderName)
{
	panel.program = new QOpenGLShaderProgram();

	if (!panel.program->addShaderFromSourceFile(QOpenGLShader::Vertex, QString("data/shaders/%1.vert").arg(shaderName)))
		return false;

	if (!panel.program->addShaderFromSourceFile(QOpenGLShader::Fragment, QString("data/shaders/%1.frag").arg(shaderName)))
		return false;

	if (!panel.program->link())
		return false;

	if ((panel.vertexMatrixUniform = panel.program->uniformLocation("vertexMatrix")) == -1)
		qWarning("Could not find vertexMatrix uniform");

	if ((panel.vertexPositionAttribute = panel.program->attributeLocation("vertexPosition")) == -1)
		qWarning("Could not find vertexPosition attribute");

	if ((panel.vertexTextureCoordinateAttribute = panel.program->attributeLocation("vertexTextureCoordinate")) == -1)
		qWarning("Could not find vertexTextureCoordinate attribute");

	if ((panel.textureSamplerUniform = panel.program->uniformLocation("textureSampler")) == -1)
		qWarning("Could not find textureSampler uniform");

	panel.textureWidthUniform = panel.program->uniformLocation("textureWidth");
	panel.textureHeightUniform = panel.program->uniformLocation("textureHeight");
	panel.texelWidthUniform = panel.program->uniformLocation("texelWidth");
	panel.texelHeightUniform = panel.program->uniformLocation("texelHeight");

	return true;
}

void Renderer::loadBuffer(Panel& panel, GLfloat* buffer, size_t size)
{
	panel.buffer = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
	panel.buffer->setUsagePattern(QOpenGLBuffer::StaticDraw);
	panel.buffer->create();
	panel.buffer->bind();
	panel.buffer->allocate(buffer, (int)(sizeof(GLfloat) * size));
	panel.buffer->release();
}

void Renderer::startRendering(double currentTime, double frameTime, double spareTime, double decoderTime, double stabilizerTime, double encoderTime)
{
	renderTimer.restart();

	this->currentTime = currentTime;

	averageFps.addMeasurement(1000.0 / frameTime);
	averageFrameTime.addMeasurement(frameTime);
	averageDecodeTime.addMeasurement(decoderTime);
	averageStabilizeTime.addMeasurement(stabilizerTime);
	averageRenderTime.addMeasurement(lastRenderTime);
	averageEncodeTime.addMeasurement(encoderTime);
	averageSpareTime.addMeasurement(spareTime);

	paintDevice->setSize(QSize(windowWidth, windowHeight));

	glViewport(0, 0, windowWidth, windowHeight);
	glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void Renderer::uploadFrameData(const FrameData& frameData)
{
	QOpenGLPixelTransferOptions options;

	options.setRowLength((int)(frameData.rowLength / 4));
	options.setImageHeight(frameData.height);
	options.setAlignment(1);

	videoPanel.texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, frameData.data, &options);
}

void Renderer::renderAll()
{
	if (isEncoding)
		outputFramebuffer->bind();

	if (renderMode == RenderMode::All || renderMode == RenderMode::Video)
		renderVideoPanel();

	if (renderMode == RenderMode::All || renderMode == RenderMode::Map)
	{
		renderMapPanel();
		renderRoute(routeManager->getDefaultRoute());

		if (mapPanel.clippingEnabled)
		{
			int mapRightBorderX = (int)(mapPanel.relativeWidth * windowWidth + 0.5);

			painter->begin(paintDevice);
			painter->setPen(QColor(0, 0, 0));
			painter->drawLine(mapRightBorderX, 0, mapRightBorderX, (int)windowHeight);
			painter->end();
		}
	}

	if (showInfoPanel)
		renderInfoPanel();

	if (isEncoding)
		outputFramebuffer->release();
}

void Renderer::stopRendering()
{
	lastRenderTime = renderTimer.nsecsElapsed() / 1000000.0;
}

FrameData Renderer::getRenderedFrame()
{
	QOpenGLFramebufferObject* sourceFbo = outputFramebuffer;

	// pixels cannot be directly read from a multisampled framebuffer
	// copy the framebuffer to a non-multisampled framebuffer and continue
	if (sourceFbo->format().samples() != 0)
	{
		QRect rect(0, 0, windowWidth, windowHeight);
		QOpenGLFramebufferObject::blitFramebuffer(outputFramebufferNonMultisample, rect, sourceFbo, rect);
		sourceFbo = outputFramebufferNonMultisample;
	}

	sourceFbo->bind();
	glReadPixels(0, 0, windowWidth, windowHeight, GL_RGBA, GL_UNSIGNED_BYTE, renderedFrameData.data);
	sourceFbo->release();

	return renderedFrameData;
}

void Renderer::renderVideoPanel()
{
	videoPanel.vertexMatrix.setToIdentity();

	if (!shouldFlipOutput)
		videoPanel.vertexMatrix.ortho(-windowWidth / 2, windowWidth / 2, -windowHeight / 2, windowHeight / 2, 0.0f, 1.0f);
	else
		videoPanel.vertexMatrix.ortho(-windowWidth / 2, windowWidth / 2, windowHeight / 2, -windowHeight / 2, 0.0f, 1.0f);

	if (renderMode != RenderMode::Video)
	{
		videoPanel.offsetX = (windowWidth / 2.0) - (((1.0 - mapPanel.relativeWidth) * windowWidth) / 2.0);
		videoPanel.scale = ((1.0 - mapPanel.relativeWidth) * windowWidth) / videoPanel.textureWidth;
	}
	else
	{
		videoPanel.offsetX = 0.0;
		videoPanel.scale = windowWidth / videoPanel.textureWidth;
	}

	if (videoPanel.scale * videoPanel.textureHeight > windowHeight)
		videoPanel.scale = windowHeight / videoPanel.textureHeight;

	videoPanel.vertexMatrix.translate(videoPanel.offsetX, videoPanel.offsetY); // window coordinate units
	videoPanel.vertexMatrix.translate( // scaled map pixel units
		videoPanel.x + videoPanel.userX + videoStabilizer->getX() * videoPanel.textureWidth * videoPanel.scale * videoPanel.userScale,
		videoPanel.y + videoPanel.userY - videoStabilizer->getY() * videoPanel.textureHeight * videoPanel.scale * videoPanel.userScale);
	videoPanel.vertexMatrix.rotate(videoPanel.angle + videoPanel.userAngle - videoStabilizer->getAngle(), 0.0f, 0.0f, 1.0f);
	videoPanel.vertexMatrix.scale(videoPanel.scale * videoPanel.userScale);

	if (fullClearRequested)
	{
		glClearColor(videoPanel.clearColor.redF(), videoPanel.clearColor.greenF(), videoPanel.clearColor.blueF(), 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		fullClearRequested = false;
	}

	if (videoPanel.clippingEnabled)
	{
		double videoPanelWidth = videoPanel.scale * videoPanel.userScale * videoPanel.textureWidth;
		double videoPanelHeight = videoPanel.scale * videoPanel.userScale * videoPanel.textureHeight;
		double leftMargin = (windowWidth - videoPanelWidth) / 2.0;
		double bottomMargin = (windowHeight - videoPanelHeight) / 2.0;

		glEnable(GL_SCISSOR_TEST);

		glScissor((int)(leftMargin + videoPanel.x + videoPanel.userX + videoPanel.offsetX + 0.5),
			(int)(bottomMargin + videoPanel.y + videoPanel.userY + videoPanel.offsetY + 0.5),
			(int)(videoPanelWidth + 0.5),
			(int)(videoPanelHeight + 0.5));
	}

	if (videoPanel.clearingEnabled)
	{
		glClearColor(videoPanel.clearColor.redF(), videoPanel.clearColor.greenF(), videoPanel.clearColor.blueF(), 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	renderPanel(videoPanel);
	glDisable(GL_SCISSOR_TEST);
}

void Renderer::renderMapPanel()
{
	mapPanel.vertexMatrix.setToIdentity();

	if (!shouldFlipOutput)
		mapPanel.vertexMatrix.ortho(-windowWidth / 2, windowWidth / 2, -windowHeight / 2, windowHeight / 2, 0.0f, 1.0f);
	else
		mapPanel.vertexMatrix.ortho(-windowWidth / 2, windowWidth / 2, windowHeight / 2, -windowHeight / 2, 0.0f, 1.0f);

	if (renderMode != RenderMode::Map)
		mapPanel.offsetX = -((windowWidth / 2.0) - ((mapPanel.relativeWidth * windowWidth) / 2.0));
	else
		mapPanel.offsetX = 0.0;

	mapPanel.vertexMatrix.translate(mapPanel.offsetX, mapPanel.offsetY); // window coordinate units
	mapPanel.vertexMatrix.rotate(mapPanel.angle + mapPanel.userAngle + routeManager->getAngle(), 0.0f, 0.0f, 1.0f);
	mapPanel.vertexMatrix.scale(mapPanel.scale * mapPanel.userScale * routeManager->getScale());
	mapPanel.vertexMatrix.translate(mapPanel.x + mapPanel.userX + routeManager->getX(), mapPanel.y + mapPanel.userY + routeManager->getY()); // map pixel units

	mapPanel.clippingEnabled = (renderMode == RenderMode::All);

	if (fullClearRequested)
	{
		glClearColor(mapPanel.clearColor.redF(), mapPanel.clearColor.greenF(), mapPanel.clearColor.blueF(), 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		fullClearRequested = false;
	}

	if (mapPanel.clippingEnabled)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(0, 0, (int)(mapPanel.relativeWidth * windowWidth + 0.5), (int)windowHeight);
	}

	if (mapPanel.clearingEnabled)
	{
		glClearColor(mapPanel.clearColor.redF(), mapPanel.clearColor.greenF(), mapPanel.clearColor.blueF(), 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}

	renderPanel(mapPanel);
	glDisable(GL_SCISSOR_TEST);
}

void Renderer::renderPanel(const Panel& panel)
{
	panel.program->bind();

	if (panel.vertexMatrixUniform >= 0)
		panel.program->setUniformValue((GLuint)panel.vertexMatrixUniform, panel.vertexMatrix);

	if (panel.textureSamplerUniform >= 0)
		panel.program->setUniformValue((GLuint)panel.textureSamplerUniform, 0);

	if (panel.textureWidthUniform >= 0)
		panel.program->setUniformValue((GLuint)panel.textureWidthUniform, (float)panel.textureWidth);

	if (panel.textureHeightUniform >= 0)
		panel.program->setUniformValue((GLuint)panel.textureHeightUniform, (float)panel.textureHeight);

	if (panel.texelWidthUniform >= 0)
		panel.program->setUniformValue((GLuint)panel.texelWidthUniform, (float)panel.texelWidth);

	if (panel.texelHeightUniform >= 0)
		panel.program->setUniformValue((GLuint)panel.texelHeightUniform, (float)panel.texelHeight);

	panel.buffer->bind();
	panel.texture->bind();

	int* textureCoordinateOffset = (int*)(sizeof(GLfloat) * 12);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(panel.vertexPositionAttribute, 3, GL_FLOAT, GL_FALSE, 0, 0);
	glVertexAttribPointer(panel.vertexTextureCoordinateAttribute, 2, GL_FLOAT, GL_FALSE, 0, textureCoordinateOffset);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	panel.texture->release();
	panel.buffer->release();
	panel.program->release();
}

void Renderer::renderRoute(const Route& route)
{
	QMatrix m;
	m.translate(windowWidth / 2.0, windowHeight / 2.0);
	m.translate(mapPanel.offsetX, mapPanel.offsetY);
	m.rotate(-(mapPanel.angle + mapPanel.userAngle + routeManager->getAngle()));
	m.scale(mapPanel.scale * mapPanel.userScale * routeManager->getScale(), mapPanel.scale * mapPanel.userScale * routeManager->getScale());
	m.translate(mapPanel.x + mapPanel.userX + routeManager->getX(), -(mapPanel.y + mapPanel.userY + routeManager->getY()));

	painter->begin(paintDevice);
	painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform | QPainter::HighQualityAntialiasing);

	if (renderMode != RenderMode::Map)
	{
		painter->setClipping(true);
		painter->setClipRect(0, 0, (int)(mapPanel.relativeWidth * windowWidth + 0.5), (int)windowHeight);
	}

	painter->setWorldMatrix(m);

	if (route.wholeRouteRenderMode == RouteRenderMode::Normal)
	{
		QPen wholeRoutePen;
		wholeRoutePen.setWidthF(route.wholeRouteWidth * route.userScale);
		wholeRoutePen.setColor(route.wholeRouteColor);
		wholeRoutePen.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
		wholeRoutePen.setCapStyle(Qt::PenCapStyle::RoundCap);

		painter->setPen(wholeRoutePen);
		painter->setBrush(Qt::NoBrush);
		painter->drawPath(route.wholeRoutePath);
	}

	if (route.wholeRouteRenderMode == RouteRenderMode::Pace)
	{
		QPen paceRoutePen;
		paceRoutePen.setWidthF(route.wholeRouteWidth * route.userScale);
		paceRoutePen.setJoinStyle(Qt::PenJoinStyle::RoundJoin);
		paceRoutePen.setCapStyle(Qt::PenCapStyle::RoundCap);

		painter->setBrush(Qt::NoBrush);

		for (size_t i = 1; i < routeManager->getDefaultRoute().routePoints.size(); ++i)
		{
			RoutePoint rp1 = routeManager->getDefaultRoute().routePoints.at(i - 1);
			RoutePoint rp2 = routeManager->getDefaultRoute().routePoints.at(i);

			paceRoutePen.setColor(rp2.color);

			painter->setPen(paceRoutePen);
			painter->drawLine(rp1.position, rp2.position);
		}
	}

	if (route.showControls)
	{
		QPen controlPen;
		controlPen.setWidthF(route.controlBorderWidth * route.userScale);
		controlPen.setColor(route.controlBorderColor);

		double controlRadius = route.controlRadius * route.userScale;

		painter->setPen(controlPen);
		painter->setBrush(Qt::NoBrush);

		for (const QPointF& controlPosition : routeManager->getDefaultRoute().controlPositions)
			painter->drawEllipse(controlPosition, controlRadius, controlRadius);
	}

	if (route.showRunner)
	{
		QPen runnerPen;
		QBrush runnerBrush;
		runnerPen.setWidthF(route.runnerBorderWidth * route.userScale);
		runnerPen.setColor(route.runnerBorderColor);
		runnerBrush.setColor(route.runnerColor);
		runnerBrush.setStyle(Qt::SolidPattern);

		double runnerRadius = (((route.wholeRouteWidth / 2.0) - (route.runnerBorderWidth / 2.0)) * route.runnerScale) * route.userScale;

		painter->setPen(runnerPen);
		painter->setBrush(runnerBrush);
		painter->drawEllipse(routeManager->getDefaultRoute().runnerPosition, runnerRadius, runnerRadius);
	}

	painter->setClipping(false);
	painter->end();
}

void Renderer::renderInfoPanel()
{
	QFont font = QFont("DejaVu Sans", 8, QFont::Bold);
	QFontMetrics metrics(font);

	int textX = 10;
	int textY = 6;
	int lineHeight = metrics.height();
	int lineSpacing = metrics.lineSpacing() + 1;
	int lineWidth1 = metrics.boundingRect("control offset:").width();
	int lineWidth2 = metrics.boundingRect("99:99:99.999").width();
	int rightPartMargin = 15;
	int backgroundRadius = 10;
	int backgroundWidth = textX + backgroundRadius + lineWidth1 + rightPartMargin + lineWidth2 + 10;
	int backgroundHeight = lineSpacing * 19 + textY + 3;

	QColor textColor = QColor(255, 255, 255, 200);
	QColor textGreenColor = QColor(0, 255, 0, 200);
	QColor textRedColor = QColor(255, 0, 0, 200);

	painter->begin(paintDevice);
	painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform | QPainter::HighQualityAntialiasing);

	painter->setPen(QColor(0, 0, 0));
	painter->setBrush(QBrush(QColor(20, 20, 20, 220)));
	painter->drawRoundedRect(-backgroundRadius, -backgroundRadius, backgroundWidth, backgroundHeight, backgroundRadius, backgroundRadius);

	painter->setPen(textColor);
	painter->setFont(font);

	painter->drawText(textX, textY, lineWidth1, lineHeight, 0, "time:");

	textY += lineSpacing;

	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "fps:");
	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "frame:");
	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "decode:");
	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "stabilize:");
	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "render:");

	if (isEncoding)
		painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "encode:");
	else
		painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "spare:");

	textY += lineSpacing;

	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "render:");
	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "scroll:");

	textY += lineSpacing;

	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "video scale:");
	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "map scale:");
	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "route scale:");

	textY += lineSpacing;

	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "control offset:");
	painter->drawText(textX, textY += lineSpacing, lineWidth1, lineHeight, 0, "runner offset:");

	textX += lineWidth1 + rightPartMargin;
	textY = 6;

	QTime currentTimeTemp = QTime(0, 0, 0, 0).addMSecs((int)(currentTime * 1000.0 + 0.5));
	painter->drawText(textX, textY, lineWidth2, lineHeight, 0, currentTimeTemp.toString("HH:mm:ss.zzz"));

	textY += lineSpacing;

	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString::number(averageFps.getAverage(), 'f', 2));
	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString("%1 ms").arg(QString::number(averageFrameTime.getAverage(), 'f', 2)));
	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString("%1 ms").arg(QString::number(averageDecodeTime.getAverage(), 'f', 2)));
	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString("%1 ms").arg(QString::number(averageStabilizeTime.getAverage(), 'f', 2)));
	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString("%1 ms").arg(QString::number(averageRenderTime.getAverage(), 'f', 2)));

	if (isEncoding)
		painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString("%1 ms").arg(QString::number(averageEncodeTime.getAverage(), 'f', 2)));
	else
	{
		if (averageSpareTime.getAverage() < 0)
			painter->setPen(textRedColor);
		else if (averageSpareTime.getAverage() > 0)
			painter->setPen(textGreenColor);

		painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString("%1 ms").arg(QString::number(averageSpareTime.getAverage(), 'f', 2)));
		painter->setPen(textColor);
	}

	QString renderText;
	QString scrollText;

	switch (renderMode)
	{
		case RenderMode::All: renderText = "both"; break;
		case RenderMode::Map: renderText = "map"; break;
		case RenderMode::Video: renderText = "video"; break;
		default: renderText = "unknown"; break;
	}

	switch (inputHandler->getScrollMode())
	{
		case ScrollMode::None: scrollText = "none"; break;
		case ScrollMode::Map: scrollText = "map"; break;
		case ScrollMode::Video: scrollText = "video"; break;
		default: scrollText = "unknown"; break;
	}

	textY += lineSpacing;

	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, renderText);
	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, scrollText);

	textY += lineSpacing;

	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString::number(videoPanel.userScale, 'f', 2));
	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString::number(mapPanel.userScale, 'f', 2));
	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString::number(routeManager->getDefaultRoute().userScale, 'f', 2));

	textY += lineSpacing;

	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString("%1 s").arg(QString::number(routeManager->getDefaultRoute().controlTimeOffset, 'f', 2)));
	painter->drawText(textX, textY += lineSpacing, lineWidth2, lineHeight, 0, QString("%1 s").arg(QString::number(routeManager->getDefaultRoute().runnerTimeOffset, 'f', 2)));

	painter->end();
}

Panel& Renderer::getVideoPanel()
{
	return videoPanel;
}

Panel& Renderer::getMapPanel()
{
	return mapPanel;
}

RenderMode Renderer::getRenderMode() const
{
	return renderMode;
}

void Renderer::setRenderMode(RenderMode mode)
{
	renderMode = mode;
}

void Renderer::setFlipOutput(bool value)
{
	paintDevice->setPaintFlipped(value);
	shouldFlipOutput = value;
}

void Renderer::setIsEncoding(bool value)
{
	isEncoding = value;
}

void Renderer::toggleShowInfoPanel()
{
	showInfoPanel = !showInfoPanel;
}

void Renderer::requestFullClear()
{
	fullClearRequested = true;
}
