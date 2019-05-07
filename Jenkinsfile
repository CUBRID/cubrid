pipeline {
  agent none

  environment {
    OUTPUT_DIR = 'packages'
    TEST_REPORT = 'reports'
  }

  stages {
    stage('Build and Test') {
      parallel {
        stage('Linux Release') {
          agent {
            docker {
              image 'cubridci/cubridci:10.1'
              label 'linux'
              alwaysPull true
            }
          }
          environment {
            MAKEFLAGS = '-j'
          }
          steps {
            script {
              currentBuild.displayName = sh(returnStdout: true, script: './build.sh -v').trim()
            }
            
            echo 'Building...'
            sh '/entrypoint.sh build'
            
            echo 'Packing...'
            sh '/entrypoint.sh dist -o ${OUTPUT_DIR}'
            
            echo 'Testing...'
            sh '/entrypoint.sh test || echo "$? failed"'
          }
          post {
            always {
              archiveArtifacts "${OUTPUT_DIR}/*"
              junit "${TEST_REPORT}/*.xml"
            }
          }
        }
        
        stage('Linux Debug') {
          agent {
            docker {
              image 'cubridci/cubridci:10.1'
              label 'linux'
              alwaysPull true
            }
          }
          environment {
            MAKEFLAGS = '-j'
          }
          steps {
            echo 'Building...'
            sh '/entrypoint.sh build -m debug'
            
            echo 'Packing...'
            sh '/entrypoint.sh dist  -m debug -o ${OUTPUT_DIR}'
            
            echo 'Testing...'
            sh '/entrypoint.sh test || echo "$? failed"'
          }
          post {
            always {
              archiveArtifacts "${OUTPUT_DIR}/*"
              junit "${TEST_REPORT}/*.xml"
            }
          }
        }
        
        stage('Windows Release') {
          agent {
            node {
              label 'windows'
            }
          }
          steps {
            echo 'Building 32bit...'
            bat "win/build.bat build"

            echo 'Packing 32bit...'
            bat "win/build.bat /out ${OUTPUT_DIR}"
            
            echo 'Building 64bit...'
            bat "win/build.bat build /64"

            echo 'Packing 64bit...'
            bat "win/build.bat /out ${OUTPUT_DIR} /64"
          }
          
          post {
            always {
              archiveArtifacts "${OUTPUT_DIR}/*"
            }
          }
        }
      }
    }
  }
  
  post {
    always {
      build job: "${DEPLOY_JOB}", parameters: [string(name: 'PROJECT_NAME', value: "${JOB_NAME}")],
            propagate: false
      emailext replyTo: '$DEFAULT_REPLYTO', to: '$DEFAULT_RECIPIENTS',
               subject: '$DEFAULT_SUBJECT', body: '''${JELLY_SCRIPT,template="html"}'''
    }
  }
}
