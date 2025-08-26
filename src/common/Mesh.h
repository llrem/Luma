#include <memory>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/Importer.hpp>
#include <assimp/DefaultLogger.hpp>
#include <assimp/LogStream.hpp>
#include <glm.hpp>

// Assimp导入选项
namespace {
    unsigned int ImportFlags = 
	    aiProcess_CalcTangentSpace |
	    aiProcess_Triangulate |
	    aiProcess_SortByPType |
	    aiProcess_PreTransformVertices |
	    aiProcess_GenNormals |
	    aiProcess_GenUVCoords |
	    aiProcess_OptimizeMeshes |
	    aiProcess_Debone |
	    aiProcess_ValidateDataStructure;
}

// Assimp日志输出器
struct LogStream : public Assimp::LogStream {
    static void initialize() {
        if (Assimp::DefaultLogger::isNullLogger()) {
            Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE);
            Assimp::DefaultLogger::get()->attachStream(new LogStream, Assimp::Logger::Err | Assimp::Logger::Warn);
        }
    }
    void write(const char *message) override { 
        std::fprintf(stderr, "Assimp: %s", message); 
    }
};


class Mesh {
public:
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec3 tangent;
        glm::vec3 bitangent;
        glm::vec2 texcoord;
    };

    struct Face {
        uint32_t v1, v2, v3;
    };

    static const int NumAttributes = 5;

    std::vector<Vertex>& vertices() { return mVertices; }
    std::vector<Face>& faces() { return mFaces; }

    static std::shared_ptr<Mesh> fromFile(std::string filename) {
        LogStream::initialize();

        std::shared_ptr<Mesh> mesh;
        Assimp::Importer importer;

        const aiScene* scene = importer.ReadFile(filename, ImportFlags);
        if (scene && scene->HasMeshes()) {
            mesh = std::shared_ptr<Mesh>(new Mesh{scene->mMeshes[0]});
        } else {
            throw std::runtime_error("Failed to load mesh file: " + filename);
        }
        return mesh;
    }

    static std::shared_ptr<Mesh> fromString(std::string data) {
        LogStream::initialize();

        std::shared_ptr<Mesh> mesh;
        Assimp::Importer importer;

        const aiScene *scene = importer.ReadFileFromMemory(data.c_str(), data.length(), ImportFlags, "nff");
        if (scene && scene->HasMeshes()) {
            mesh = std::shared_ptr<Mesh>(new Mesh{scene->mMeshes[0]});
        } else {
            throw std::runtime_error("Failed to create mesh from string: " + data);
        }
        return mesh;
    }

private:
    Mesh(struct aiMesh* mesh) {
        mVertices.reserve(mesh->mNumVertices);
        for (size_t i = 0; i < mVertices.capacity(); ++i) {
            Vertex vertex;
            vertex.position = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};
            vertex.normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
            if (mesh->HasTangentsAndBitangents()) {
                vertex.tangent = {mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z};
                vertex.bitangent = {mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z};
            }
            if (mesh->HasTextureCoords(0)) {
                vertex.texcoord = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
            }
            mVertices.push_back(vertex);
        }

        mFaces.reserve(mesh->mNumFaces);
        for (size_t i = 0; i < mFaces.capacity(); ++i) {
            mFaces.push_back({mesh->mFaces[i].mIndices[0], mesh->mFaces[i].mIndices[1], mesh->mFaces[i].mIndices[2]});
        }
    }

    std::vector<Vertex> mVertices;
    std::vector<Face> mFaces;
};