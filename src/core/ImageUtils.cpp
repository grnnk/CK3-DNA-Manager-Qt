#include "core/ImageUtils.h"

#include <QBuffer>
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <QUrl>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <objbase.h>
#  include <wincodec.h>
#  include <windows.h>
#endif

namespace {

const QStringList &imageSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("jfif"), QStringLiteral("webp"), QStringLiteral("bmp"),
        QStringLiteral("gif"),
    };
    return suffixes;
}

#ifdef Q_OS_WIN

template <typename T>
void releaseCom(T *&pointer)
{
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

class ScopedCom
{
public:
    ScopedCom()
        : m_initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
    }
    ~ScopedCom()
    {
        if (m_initialized)
            CoUninitialize();
    }
    ScopedCom(const ScopedCom &) = delete;
    ScopedCom &operator=(const ScopedCom &) = delete;

private:
    bool m_initialized;
};

IWICImagingFactory *createWicFactory()
{
    IWICImagingFactory *factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return nullptr;
    }
    return factory;
}

QImage imageFromWicDecoder(IWICImagingFactory *factory, IWICBitmapDecoder *decoder)
{
    if (!factory || !decoder)
        return {};

    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    QImage image;

    if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame
        && SUCCEEDED(factory->CreateFormatConverter(&converter)) && converter
        && SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                           WICBitmapDitherTypeNone, nullptr, 0.0,
                                           WICBitmapPaletteTypeCustom))) {
        UINT width = 0;
        UINT height = 0;
        constexpr UINT maxDimension = 20000;
        if (SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0
            && width <= maxDimension && height <= maxDimension) {
            QImage decoded(int(width), int(height), QImage::Format_ARGB32_Premultiplied);
            if (!decoded.isNull()) {
                const UINT stride = width * 4;
                if (SUCCEEDED(converter->CopyPixels(nullptr, stride, stride * height,
                                                    decoded.bits()))) {
                    image = decoded;
                }
            }
        }
    }

    releaseCom(converter);
    releaseCom(frame);
    return image;
}

QImage imageFromWicFile(const QString &path)
{
    ScopedCom com;
    IWICImagingFactory *factory = createWicFactory();
    if (!factory)
        return {};

    IWICBitmapDecoder *decoder = nullptr;
    QImage image;
    if (SUCCEEDED(factory->CreateDecoderFromFilename(
            reinterpret_cast<const wchar_t *>(path.utf16()), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder))) {
        image = imageFromWicDecoder(factory, decoder);
    }

    releaseCom(decoder);
    releaseCom(factory);
    return image;
}

QImage imageFromWicBytes(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return {};

    ScopedCom com;
    IWICImagingFactory *factory = createWicFactory();
    if (!factory)
        return {};

    QImage image;
    IStream *stream = nullptr;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) && stream) {
        ULONG written = 0;
        if (SUCCEEDED(stream->Write(bytes.constData(), ULONG(bytes.size()), &written))
            && written == ULONG(bytes.size())) {
            LARGE_INTEGER start;
            start.QuadPart = 0;
            stream->Seek(start, STREAM_SEEK_SET, nullptr);

            IWICBitmapDecoder *decoder = nullptr;
            if (SUCCEEDED(factory->CreateDecoderFromStream(
                    stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) {
                image = imageFromWicDecoder(factory, decoder);
            }
            releaseCom(decoder);
        }
        releaseCom(stream);
    }

    releaseCom(factory);
    return image;
}

#endif // Q_OS_WIN

} // namespace

namespace ImageUtils {

QImage loadImage(const QString &path, QString *error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (!image.isNull())
        return image;

#ifdef Q_OS_WIN
    const QImage wicImage = imageFromWicFile(path);
    if (!wicImage.isNull())
        return wicImage;
#endif

    if (error) {
        *error = QStringLiteral("Cannot load image \"%1\": %2")
                     .arg(path, reader.errorString());
        if (path.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive)) {
#ifdef Q_OS_WIN
            *error += QStringLiteral(" WebP support requires the Qt Image Formats plugin "
                                     "or the Microsoft WebP Image Extensions.");
#else
            *error += QStringLiteral(" WebP support requires the Qt Image Formats plugin "
                                     "(for example qt6-image-formats-plugins).");
#endif
        }
    }
    return {};
}

QImage imageFromBytes(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return {};
    QImage image;
    if (image.loadFromData(bytes))
        return image;
#ifdef Q_OS_WIN
    return imageFromWicBytes(bytes);
#else
    return {};
#endif
}

bool isSupportedImagePath(const QString &path)
{
    return imageSuffixes().contains(QFileInfo(path).suffix().toLower());
}

QStringList supportedImageSuffixes()
{
    return imageSuffixes();
}

bool hasClipboardImage()
{
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
    if (!mimeData)
        return false;
    if (mimeData->hasImage())
        return true;
    if (!mimeData->hasUrls())
        return false;
    for (const QUrl &url : mimeData->urls()) {
        if (url.isLocalFile() && isSupportedImagePath(url.toLocalFile()))
            return true;
    }
    return false;
}

QImage imageFromClipboard()
{
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
    if (!mimeData)
        return {};
    if (mimeData->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull())
            return image;
    }
    if (!mimeData->hasUrls())
        return {};
    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (!isSupportedImagePath(path))
            continue;
        const QImage image = loadImage(path);
        if (!image.isNull())
            return image;
    }
    return {};
}

bool hasClipboardText()
{
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
    return mimeData && mimeData->hasText();
}

QByteArray encodePng(const QImage &image)
{
    if (image.isNull())
        return {};

    QImage normalized = image;
    constexpr int maxDimension = 1600;
    if (normalized.width() > maxDimension || normalized.height() > maxDimension) {
        normalized = normalized.scaled(maxDimension, maxDimension,
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (normalized.format() != QImage::Format_ARGB32
        && normalized.format() != QImage::Format_ARGB32_Premultiplied) {
        normalized = normalized.convertToFormat(QImage::Format_ARGB32);
    }

    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    normalized.save(&buffer, "PNG");
    return result;
}

QByteArray makeThumbnail(const QImage &image, const QSize &target)
{
    if (image.isNull() || !target.isValid())
        return {};

    const QImage scaled = image.scaled(target, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation);
    const int x = qMax(0, (scaled.width() - target.width()) / 2);
    const int y = qMax(0, (scaled.height() - target.height()) / 2);
    const QImage cropped = scaled.copy(x, y, target.width(), target.height());
    return encodePng(cropped);
}

QSize thumbnailSize()
{
    return QSize(96, 120);
}

} // namespace ImageUtils
