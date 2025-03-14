#include "ImageService.hpp"
#include "FileUtils.hpp"
#include "LoadingAnimation.hpp"
#include "cubeMap2Equrec.hpp"

using namespace std;

// Loads an image from a given path
cv::Mat loadImage(const string &imagePath) {
  fs::path path(imagePath);
  if (!fs::exists(path) || !fs::is_regular_file(path)) {
    throw runtime_error("Missing or invalid image: " + path.string());
  }

  cv::Mat image = cv::imread(path.string());
  if (image.empty()) {
    throw runtime_error("Unable to load the image: " + imagePath);
  }

  {
    std::lock_guard<std::mutex> lock(coutMutex); // Empêche l'animation d'écrire
    pauseAnimation = true; // Stoppe l'animation temporairement
    std::cout << "\r" << std::string(30, ' ') << "\r"; // Efface l'animation
    std::cout << "Image chargée: " << imagePath << std::endl;
    pauseAnimation = false; // Relance l'animation
  }
  return image;
}

// Loads the cube map using the previous functions
vector<cv::Mat> loadCubeMap(const string &imagesFolder) {
  // List of expected file names
  const vector<string> faceNames = {"left.jpg", "front.jpg",  "right.jpg",
                                    "back.jpg", "bottom.jpg", "top.jpg"};
  // Loading the cube map images
  vector<cv::Mat> cubeMapFaces;
  for (const auto &face : faceNames) {
    fs::path imagePath = fs::path(imagesFolder) / face;
    cubeMapFaces.push_back(loadImage(imagePath.string()));
  }

  return cubeMapFaces;
}

cv::Mat convertCubeMapEnEquirect(const vector<cv::Mat> &cubeFacesList) {
  // Correspondence of the cube map faces to their positions.
  // Extract images from cube map from a single file with the following format:
  //		+----+----+----+
  //		| Y+ | X+ | Y- |
  //		+----+----+----+
  //		| X- | Z- | Z+ |
  //		+----+----+----+
  cv::Mat posY = cubeFacesList[3];
  cv::Mat posX = cubeFacesList[2];
  cv::Mat negY = cubeFacesList[0];
  cv::Mat negX = cubeFacesList[1];
  cv::Mat negZ = cubeFacesList[4];
  cv::Mat posZ = cubeFacesList[5];

  const int output_width = cubeFacesList[0].rows * 4;
  const int output_height = cubeFacesList[0].rows * 2;
  const int square_length = cubeFacesList[0].rows;

  // Placeholder image for the result
  cv::Mat destination(output_height, output_width, CV_8UC3,
                      cv::Scalar(255, 255, 255));

  auto begin = chrono::high_resolution_clock::now();

#pragma omp parallel for
  // 1. Loop through all of the pixels in the output image
  for (int j = 0; j < destination.cols; j++) {
    // #pragma omp parallel for
    for (int i = 0; i < destination.rows; i++) {
      // 2. Get the normalised u,v coordinates for the current pixel
      float U = static_cast<float>(j) / (output_width - 1); // 0..1
      float V = static_cast<float>(i) /
                (output_height - 1); // No need for 1-... as the image output
      // needs to start from the top anyway.
      // 3. Taking the normalised cartesian coordinates calculate the polar
      // coordinate for the current pixel
      float theta =
          U * 2 *
          M_PI; // M_PI is defined in math.h when _USE_MATH_DEFINES is defined
      float phi =
          V *
          M_PI; // M_PI is defined in math.h when _USE_MATH_DEFINES is defined
      // 4. calculate the 3D cartesian coordinate which has been projected to
      // a cubes face
      cart2D cart = convertEquirectUVtoUnit2D(theta, phi, square_length);

      // 5. use this pixel to extract the colour
      cv::Vec3b val;
      // clamp the values to be inside the image
      if (cart.x < 0)
        cart.x = 0;
      else if (cart.x >= square_length)
        cart.x = square_length - 1;
      if (cart.y < 0)
        cart.y = 0;
      else if (cart.y >= square_length)
        cart.y = square_length - 1;

      if (cart.faceIndex == X_POS) {
        val = posX.at<cv::Vec3b>(cart.y, cart.x);
      } else if (cart.faceIndex == X_NEG) {
        val = negX.at<cv::Vec3b>(cart.y, cart.x);
      } else if (cart.faceIndex == Y_POS) {
        val = posY.at<cv::Vec3b>(cart.y, cart.x);
      } else if (cart.faceIndex == Y_NEG) {
        val = negY.at<cv::Vec3b>(cart.y, cart.x);
      } else if (cart.faceIndex == Z_POS) {
        val = posZ.at<cv::Vec3b>(cart.y, cart.x);
      } else if (cart.faceIndex == Z_NEG) {
        val = negZ.at<cv::Vec3b>(cart.y, cart.x);
      }

      destination.at<cv::Vec3b>(i, j) = val;
    }
  }
  auto end = chrono::high_resolution_clock::now();
  chrono::duration<double> diff = end - begin;
  processingDone = true;
  stopAnimation();
  std::cout << std::endl
            << "Temps de calcul : " << diff.count() << " s" << std::endl;
  return destination;
}

void saveImage(const cv::Mat &image, const string &filePath) {
  if (image.empty()) {
    throw runtime_error("The image is empty. Unable to save the image.");
  }

  fs::path destinationFolder = fs::path(filePath).parent_path();
  verifyFolderExists(destinationFolder.string());
  verifyPermissions(destinationFolder);

  // --- Apply Gaussian Blur ---
  // Create a copy because the Gaussian blur changes the image.
  cv::Mat blurredImage;
  cv::GaussianBlur(image, blurredImage, cv::Size(3, 3), 0, 0);

  // --- JPEG Compression Parameters ---
  std::vector<int> compression_params;
  compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
  compression_params.push_back(
      85); // Adjust the quality here (0-100, higher is better quality)
  compression_params.push_back(cv::IMWRITE_JPEG_PROGRESSIVE);
  compression_params.push_back(1);
  compression_params.push_back(cv::IMWRITE_JPEG_OPTIMIZE);
  compression_params.push_back(1);

  // Attempt to save the image
  if (!cv::imwrite(filePath, blurredImage, compression_params)) {
    throw runtime_error("Impossible to save the image: " + filePath);
  }

  std::lock_guard<std::mutex> lock(coutMutex); // Empêche l'animation d'écrire
  stopAnimation();
  cout << "Image sauvegardée avec succès : " << filePath << endl;
}
