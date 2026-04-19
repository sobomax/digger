node('microsippy') {
  timestamps {
    def builderHome = env.WORKSPACE
    def IDF_PATH = "${builderHome}/ESP8266_RTOS_SDK"
    def P_IDF_PATH = "${builderHome}/ESP8266_RTOS_SDK.patched"
    def IDF_TOOLCHAIN = env.IDF_TOOLCHAIN
    stage('Prepare/Checkout') { // for display purposes
      dir('microsippy') {
        git branch: 'master', url: 'https://github.com/sobomax/microsippy.git'
      }
      dir('ESP8266_RTOS_SDK') {
        checkout(
          [
            $class: 'GitSCM',
            branches: [
              [
                name: '*/master'
              ]
            ],
            doGenerateSubmoduleConfigurations: false,
            extensions: [
              [
                $class: 'SubmoduleOption',
                disableSubmodules: false,
                parentCredentials: true,
                recursiveSubmodules: true,
                trackingSubmodules: false
              ],
              [$class: 'LocalBranch', localBranch: "**"]
            ],
            submoduleCfg: [],
            userRemoteConfigs: [
              [
                url: 'https://github.com/espressif/ESP8266_RTOS_SDK.git'
              ]
            ]
          ]
        )
      }
      dir('ESP8266_RTOS_SDK.patched') {
        git branch: 'master', url: 'https://github.com/sobomax/ESP8266_RTOS_SDK.git'
      }
    }

    stage('Clear Workspace') {
      sh "rm -rf ${builderHome}/microsippy/src/build"
    }

    stage('Merge') {
      sh "git -C ${IDF_PATH} remote remove patched || true"
      sh "git -C ${IDF_PATH} remote add -f patched ${P_IDF_PATH}"
      sh "git -C ${IDF_PATH} fetch patched"
      sh "git -C ${IDF_PATH} merge --m 'Merge our patches.' patched/master"
    }

    stage('Build') {
      withCredentials([
       [$class: 'StringBinding', credentialsId:'2e13b399-dc4e-4d14-ae6e-1383b6d3e0ad',
        variable: 'WIFI_SSID'],
       [$class: 'StringBinding', credentialsId:'37542a38-ed60-45ed-87a8-33fd512ff740',
        variable: 'WIFI_PASSWORD']
       ]) {
        // Run the  build
        sh "IDF_PATH=${IDF_PATH} IDF_TOOLCHAIN=${IDF_TOOLCHAIN} ${builderHome}/microsippy/scripts/do-build.sh ${builderHome}/microsippy/platforms/freertos/esp8266"
      }
    }

    stage('Flash') {
      // Flash the board
      sh "IDF_PATH=${IDF_PATH} IDF_TOOLCHAIN=${IDF_TOOLCHAIN} ${builderHome}/microsippy/scripts/do-build.sh ${builderHome}/microsippy/platforms/freertos/esp8266 flash"
    }

    stage('Test') {
      ansiColor('xterm') {
        sh "IDF_PATH=${IDF_PATH} IDF_TOOLCHAIN=${IDF_TOOLCHAIN} ${builderHome}/microsippy/scripts/do-test.sh ${builderHome}/microsippy/platforms/freertos/esp8266"
      }
    }
  }
}

