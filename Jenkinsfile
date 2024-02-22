pipeline {
  agent none

  triggers {
    pollSCM('H 21 * * 1,2,4,5,7')
  }

  environment {
    OUTPUT_DIR = 'packages'
    TEST_REPORT = 'reports'
    JUNIT_REQUIRED = 'true'
  }

  stages {
    stage('Build and Test') {
      parallel {
        stage('Linux Release') {
          agent {
            docker {
              image 'cubridci/cubridci:develop'
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
            sh 'scl enable devtoolset-8 -- /entrypoint.sh build'

            echo 'Packing...'
            sh "scl enable devtoolset-8 -- /entrypoint.sh dist -o ${OUTPUT_DIR}"

            script {
              if (env.BRANCH_NAME ==~ /^feature\/.*/) {
                echo 'Skip testing for feature branch'
                JUNIT_REQUIRED = 'false'
              } else {
            	echo 'Testing...'
            	sh '/entrypoint.sh test || echo "$? failed"'
              }
            }
          }
          post {
            always {
              script {
                archiveArtifacts "${OUTPUT_DIR}/*"
                if (env.JUNIT_REQUIRED == 'true' && fileExists("${TEST_REPORT}/summary.xml")) {
                  junit "${TEST_REPORT}/*.xml"
                } else {
                  echo 'Skip junit for feature branch'
                }
              }
            }
          }
        }

        stage('Linux Debug') {
          agent {
            docker {
              image 'cubridci/cubridci:develop'
              label 'linux'
              alwaysPull true
            }
          }
          environment {
            MAKEFLAGS = '-j'
          }
          steps {
            echo 'Building...'
            sh 'scl enable devtoolset-8 -- /entrypoint.sh build -m debug'
            
            echo 'Packing...'
            sh "scl enable devtoolset-8 -- /entrypoint.sh dist -m debug -o ${OUTPUT_DIR}"

            script {
              if (env.BRANCH_NAME ==~ /^feature\/.*/) {
                echo 'Skip testing for feature branch'
                JUNIT_REQUIRED = 'false'
              } else {
            	echo 'Testing...'
            	sh '/entrypoint.sh test || echo "$? failed"'
              }
            }
          }
          post {
            always {
              script {
                archiveArtifacts "${OUTPUT_DIR}/*"
                if (env.JUNIT_REQUIRED == 'true' && fileExists("${TEST_REPORT}/summary.xml")) {
                  junit "${TEST_REPORT}/*.xml"
                } else {
                  echo 'Skip junit for feature branch'
                }
              }
            }
          }
        }

        stage('Windows Release') {
          when {
            not {
              // Skip Windows Release stage for feature branches
              branch 'feature/*'
            }
          }
          agent {
            node {
              label 'windows'
            }
          }
          steps {
            echo 'Building...'
            bat "win/build.bat build"

            echo 'Packing...'
            bat "win/build.bat /out ${OUTPUT_DIR} dist"
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
      script {
        if (env.BRANCH_NAME ==~ /^feature\/.*/) {
          build job: "${DEPLOY_JOB_FOR_MANUAL}", parameters: [string(name: 'PROJECT_NAME', value: "${JOB_NAME}")],
                propagate: false
        } else {
          build job: "${DEPLOY_JOB}", parameters: [string(name: 'PROJECT_NAME', value: "${JOB_NAME}")],
                propagate: false
        }
      }
      emailext replyTo: '$DEFAULT_REPLYTO', to: '$DEFAULT_RECIPIENTS',
               subject: '$DEFAULT_SUBJECT', body: '''${JELLY_SCRIPT,template="html"}'''
    }
  }
}
